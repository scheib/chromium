// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/location_bar/icon_label_bubble_view.h"

#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/views/location_bar/background_with_1_px_border.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/animation/flood_fill_ink_drop_ripple.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/animation/ink_drop_ripple.h"
#include "ui/views/border.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/widget/widget.h"

namespace {

// Amount of space on either side of the separator that appears after the label.
constexpr int kSpaceBesideSeparator = 8;

// The length of the separator's fade animation. These values are empirical.
constexpr int kFadeInDurationMs = 250;
constexpr int kFadeOutDurationMs = 175;

}  // namespace

//////////////////////////////////////////////////////////////////
// SeparatorView class

IconLabelBubbleView::SeparatorView::SeparatorView(IconLabelBubbleView* owner) {
  DCHECK(owner);
  owner_ = owner;
}

void IconLabelBubbleView::SeparatorView::OnPaint(gfx::Canvas* canvas) {
  const SkColor plain_text_color = owner_->GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_TextfieldDefaultColor);
  const SkColor separator_color = SkColorSetA(
      plain_text_color, color_utils::IsDark(plain_text_color) ? 0x59 : 0xCC);
  float x = GetLocalBounds().right() - owner_->GetPostSeparatorPadding();
  canvas->Draw1pxLine(gfx::PointF(x, GetLocalBounds().y()),
                      gfx::PointF(x, GetLocalBounds().bottom()),
                      separator_color);
}

void IconLabelBubbleView::SeparatorView::OnImplicitAnimationsCompleted() {
  if (layer() && layer()->opacity() == 1.0f)
    DestroyLayer();
}

void IconLabelBubbleView::SeparatorView::UpdateOpacity() {
  if (!visible())
    return;

  views::InkDrop* ink_drop = owner_->GetInkDrop();
  DCHECK(ink_drop);

  // If an inkdrop highlight or ripple is animating in or visible, the
  // separator should fade out.
  views::InkDropState state = ink_drop->GetTargetInkDropState();
  float opacity = 0.0f;
  float duration = kFadeOutDurationMs;
  if (!ink_drop->IsHighlightFadingInOrVisible() &&
      (state == views::InkDropState::HIDDEN ||
       state == views::InkDropState::ACTION_TRIGGERED ||
       state == views::InkDropState::DEACTIVATED)) {
    opacity = 1.0f;
    duration = kFadeInDurationMs;
  }

  if (!layer())
    SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  ui::ScopedLayerAnimationSettings animation(layer()->GetAnimator());
  animation.SetTransitionDuration(base::TimeDelta::FromMilliseconds(duration));
  animation.SetTweenType(gfx::Tween::Type::EASE_IN);
  animation.AddObserver(this);
  layer()->SetOpacity(opacity);
}

//////////////////////////////////////////////////////////////////
// IconLabelBubbleView class

IconLabelBubbleView::IconLabelBubbleView(const gfx::FontList& font_list,
                                         bool elide_in_middle)
    : CustomButton(nullptr),
      image_(new views::ImageView()),
      label_(new views::Label(base::string16(), {font_list})),
      ink_drop_container_(new views::InkDropContainerView()),
      separator_view_(new SeparatorView(this)),
      suppress_button_release_(false) {
  // Disable separate hit testing for |image_|.  This prevents views treating
  // |image_| as a separate mouse hover region from |this|.
  image_->set_can_process_events_within_subtree(false);
  image_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets(LocationBarView::kIconInteriorPadding)));
  AddChildView(image_);

  label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  if (elide_in_middle)
    label_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  AddChildView(label_);

  separator_view_->SetVisible(ShouldShowLabel());
  AddChildView(separator_view_);

  AddChildView(ink_drop_container_);
  ink_drop_container_->SetVisible(false);

  // Bubbles are given the full internal height of the location bar so that all
  // child views in the location bar have the same height. The visible height of
  // the bubble should be smaller, so use an empty border to shrink down the
  // content bounds so the background gets painted correctly.
  SetBorder(views::CreateEmptyBorder(
      gfx::Insets(LocationBarView::kBubbleVerticalPadding, 0)));

  set_notify_enter_exit_on_child(true);

  // Flip the canvas in RTL so the separator is drawn on the correct side.
  separator_view_->EnableCanvasFlippingForRTLUI(true);
}

IconLabelBubbleView::~IconLabelBubbleView() {
}

void IconLabelBubbleView::InkDropAnimationStarted() {
  separator_view_->UpdateOpacity();
}

void IconLabelBubbleView::SetLabel(const base::string16& label) {
  label_->SetText(label);
  separator_view_->SetVisible(ShouldShowLabel());
}

void IconLabelBubbleView::SetImage(const gfx::ImageSkia& image_skia) {
  image_->SetImage(image_skia);
}

bool IconLabelBubbleView::ShouldShowLabel() const {
  return label_->visible() && !label_->text().empty();
}

double IconLabelBubbleView::WidthMultiplier() const {
  return 1.0;
}

bool IconLabelBubbleView::IsShrinking() const {
  return false;
}

bool IconLabelBubbleView::ShowBubble(const ui::Event& event) {
  return false;
}

bool IconLabelBubbleView::IsBubbleShowing() const {
  return false;
}

gfx::Size IconLabelBubbleView::GetPreferredSize() const {
  // Height will be ignored by the LocationBarView.
  return GetSizeForLabelWidth(label_->GetPreferredSize().width());
}

void IconLabelBubbleView::Layout() {
  // We may not have horizontal room for both the image and the trailing
  // padding. When the view is expanding (or showing-label steady state), the
  // image. When the view is contracting (or hidden-label steady state), whittle
  // away at the trailing padding instead.
  int bubble_trailing_padding = GetPostSeparatorPadding();
  int image_width = image_->GetPreferredSize().width();
  const int space_shortage = image_width + bubble_trailing_padding - width();
  if (space_shortage > 0) {
    if (ShouldShowLabel())
      image_width -= space_shortage;
    else
      bubble_trailing_padding -= space_shortage;
  }
  image_->SetBounds(0, 0, image_width, height());

  // Compute the label bounds.  The label gets whatever size is left over after
  // accounting for the preferred image width and padding amounts.  Note that if
  // the label has zero size it doesn't actually matter what we compute its X
  // value to be, since it won't be visible.
  const int label_x = image_->bounds().right() + GetInternalSpacing();
  const int label_width =
      std::max(0, width() - label_x - bubble_trailing_padding -
                      kSpaceBesideSeparator - GetSeparatorLayoutWidth());
  label_->SetBounds(label_x, 0, label_width, height());

  const int kSeparatorHeight = 16;
  gfx::Rect separator_bounds(label_->bounds());
  separator_bounds.Inset(0, (separator_bounds.height() - kSeparatorHeight) / 2);

  float separator_width = kSpaceBesideSeparator + GetPostSeparatorPadding();
  separator_view_->SetBounds(GetLocalBounds().right() - separator_width,
                             separator_bounds.y(), separator_width,
                             kSeparatorHeight);

  gfx::Rect ink_drop_bounds = GetLocalBounds();
  if (ShouldShowLabel()) {
    ink_drop_bounds.set_width(ink_drop_bounds.width() -
                              GetPostSeparatorPadding());
  }

  ink_drop_container_->SetBoundsRect(ink_drop_bounds);
}

bool IconLabelBubbleView::OnMousePressed(const ui::MouseEvent& event) {
  suppress_button_release_ = IsBubbleShowing();
  return CustomButton::OnMousePressed(event);
}

void IconLabelBubbleView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  label_->GetAccessibleNodeData(node_data);
}

void IconLabelBubbleView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  if (!IsMouseHovered()) {
    GetInkDrop()->AnimateToState(views::InkDropState::HIDDEN);
    GetInkDrop()->SetHovered(false);
  }
  CustomButton::OnBoundsChanged(previous_bounds);
}

void IconLabelBubbleView::OnNativeThemeChanged(
    const ui::NativeTheme* native_theme) {
  label_->SetEnabledColor(GetTextColor());
  label_->SetBackgroundColor(GetParentBackgroundColor());
  SchedulePaint();
}

void IconLabelBubbleView::AddInkDropLayer(ui::Layer* ink_drop_layer) {
  ink_drop_container_->AddInkDropLayer(ink_drop_layer);
}

void IconLabelBubbleView::RemoveInkDropLayer(ui::Layer* ink_drop_layer) {
  ink_drop_container_->RemoveInkDropLayer(ink_drop_layer);
}

std::unique_ptr<views::InkDrop> IconLabelBubbleView::CreateInkDrop() {
  std::unique_ptr<views::InkDropImpl> ink_drop =
      CreateDefaultFloodFillInkDropImpl();
  ink_drop->SetShowHighlightOnFocus(true);
  ink_drop->AddObserver(this);
  return std::move(ink_drop);
}

std::unique_ptr<views::InkDropHighlight>
IconLabelBubbleView::CreateInkDropHighlight() const {
  return InkDropHostView::CreateDefaultInkDropHighlight(
      gfx::RectF(ink_drop_container_->bounds()).CenterPoint(),
      ink_drop_container_->size());
}

std::unique_ptr<views::InkDropRipple> IconLabelBubbleView::CreateInkDropRipple()
    const {
  gfx::Point center_point = GetInkDropCenterBasedOnLastEvent();
  View::ConvertPointToTarget(this, ink_drop_container_, &center_point);
  center_point.SetToMax(ink_drop_container_->origin());
  center_point.SetToMin(ink_drop_container_->bounds().bottom_right());

  return base::MakeUnique<views::FloodFillInkDropRipple>(
      ink_drop_container_->size(), center_point, GetInkDropBaseColor(),
      ink_drop_visible_opacity());
}

SkColor IconLabelBubbleView::GetInkDropBaseColor() const {
  return color_utils::DeriveDefaultIconColor(GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_TextfieldDefaultColor));
}

bool IconLabelBubbleView::IsTriggerableEvent(const ui::Event& event) {
  if (event.IsMouseEvent())
    return !IsBubbleShowing() && !suppress_button_release_;

  return true;
}

bool IconLabelBubbleView::ShouldUpdateInkDropOnClickCanceled() const {
  // The click might be cancelled because the bubble is still opened. In this
  // case, the ink drop state should not be hidden by CustomButton.
  return false;
}

void IconLabelBubbleView::NotifyClick(const ui::Event& event) {
  CustomButton::NotifyClick(event);
  OnActivate(event);
}

void IconLabelBubbleView::OnWidgetDestroying(views::Widget* widget) {
  widget->RemoveObserver(this);
}

void IconLabelBubbleView::OnWidgetVisibilityChanged(views::Widget* widget,
                                                    bool visible) {
  // |widget| is a bubble that has just got shown / hidden.
  if (!visible)
    AnimateInkDrop(views::InkDropState::HIDDEN, nullptr /* event */);
}

SkColor IconLabelBubbleView::GetParentBackgroundColor() const {
  return GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_TextfieldDefaultBackground);
}

gfx::Size IconLabelBubbleView::GetSizeForLabelWidth(int label_width) const {
  gfx::Size size(image_->GetPreferredSize());
  const bool shrinking = IsShrinking();
  // Animation continues for the last few pixels even after the label is not
  // visible in order to slide the icon into its final position. Therefore it
  // is necessary to animate |total_width| even when the background is hidden
  // as long as the animation is still shrinking.
  if (ShouldShowLabel() || shrinking) {
    const int post_label_width =
        (kSpaceBesideSeparator + GetSeparatorLayoutWidth() +
         GetPostSeparatorPadding());

    // |multiplier| grows from zero to one, stays equal to one and then shrinks
    // to zero again. The view width should correspondingly grow from zero to
    // fully showing both label and icon, stay there, then shrink to just large
    // enough to show the icon. We don't want to shrink all the way back to
    // zero, since this would mean the view would completely disappear and then
    // pop back to an icon after the animation finishes.
    const int max_width =
        size.width() + GetInternalSpacing() + label_width + post_label_width;
    const int current_width = WidthMultiplier() * max_width;
    size.set_width(shrinking ? std::max(current_width, size.width())
                             : current_width);
  }
  return size;
}

gfx::Size IconLabelBubbleView::GetMaxSizeForLabelWidth(int label_width) const {
  gfx::Size size(image_->GetPreferredSize());
  if (ShouldShowLabel() || IsShrinking()) {
    // On scale factors < 2, we reserve 1 DIP for the 1 px separator.  For
    // higher scale factors, we simply take the separator px out of the
    // kSpaceBesideSeparator region before the separator, as that results in a
    // width closer to the desired gap than if we added a whole DIP for the
    // separator px.  (For scale 2, the two methods have equal error: 1 px.)
    const int separator_width = (GetScaleFactor() >= 2) ? 0 : 1;
    const int post_label_width =
        (kSpaceBesideSeparator + separator_width + GetPostSeparatorPadding());
    size.Enlarge(GetInternalSpacing() + label_width + post_label_width, 0);
  }
  return size;
}

int IconLabelBubbleView::GetInternalSpacing() const {
  return image_->GetPreferredSize().IsEmpty()
             ? 0
             : GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING);
}

int IconLabelBubbleView::GetSeparatorLayoutWidth() const {
  // On scale factors < 2, we reserve 1 DIP for the 1 px separator.  For
  // higher scale factors, we simply take the separator px out of the
  // kSpaceBesideSeparator region before the separator, as that results in a
  // width closer to the desired gap than if we added a whole DIP for the
  // separator px.  (For scale 2, the two methods have equal error: 1 px.)
  return (GetScaleFactor() >= 2) ? 0 : 1;
}

int IconLabelBubbleView::GetPostSeparatorPadding() const {
  // The location bar will add LOCATION_BAR_ELEMENT_PADDING after us.
  return kSpaceBesideSeparator -
         GetLayoutConstant(LOCATION_BAR_ELEMENT_PADDING) -
         next_element_interior_padding_;
}

float IconLabelBubbleView::GetScaleFactor() const {
  const views::Widget* widget = GetWidget();
  // There may be no widget in tests, and in ash there may be no compositor if
  // the native view of the Widget doesn't have a parent.
  const ui::Compositor* compositor = widget ? widget->GetCompositor() : nullptr;
  return compositor ? compositor->device_scale_factor() : 1.0f;
}

bool IconLabelBubbleView::OnActivate(const ui::Event& event) {
  if (ShowBubble(event)) {
    AnimateInkDrop(views::InkDropState::ACTIVATED, nullptr);
    return true;
  }

  AnimateInkDrop(views::InkDropState::HIDDEN, nullptr);
  return false;
}

const char* IconLabelBubbleView::GetClassName() const {
  return "IconLabelBubbleView";
}
