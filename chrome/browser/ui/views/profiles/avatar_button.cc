// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/avatar_button.h"

#include <utility>

#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/views/profiles/profile_chooser_view.h"
#include "chrome/grit/theme_resources.h"
#include "components/signin/core/browser/signin_manager.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/animation/flood_fill_ink_drop_ripple.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/animation/ink_drop_mask.h"
#include "ui/views/controls/button/label_button_border.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "chrome/browser/ui/views/frame/minimize_button_metrics_win.h"
#endif

namespace {

constexpr gfx::Insets kBorderInsets(2, 8, 4, 8);

// TODO(emx): Calculate width based on caption button [http://crbug.com/716365]
constexpr int kCondensibleButtonMinWidth = 46;
// TODO(emx): Should this be calculated based on average character width?
constexpr int kCondensibleButtonMaxWidth = 98;

std::unique_ptr<views::Border> CreateThemedBorder(
    const int normal_image_set[],
    const int hot_image_set[],
    const int pushed_image_set[]) {
  std::unique_ptr<views::LabelButtonAssetBorder> border(
      new views::LabelButtonAssetBorder(views::Button::STYLE_TEXTBUTTON));

  border->SetPainter(false, views::Button::STATE_NORMAL,
                     views::Painter::CreateImageGridPainter(normal_image_set));
  border->SetPainter(false, views::Button::STATE_HOVERED,
                     views::Painter::CreateImageGridPainter(hot_image_set));
  border->SetPainter(false, views::Button::STATE_PRESSED,
                     views::Painter::CreateImageGridPainter(pushed_image_set));

  border->set_insets(kBorderInsets);

  return std::move(border);
}

// This class draws the border (and background) of the avatar button for
// "themed" browser windows, i.e. OpaqueBrowserFrameView. Currently it's only
// used on Linux as the shape specifically matches the Linux caption buttons.
// TODO(estade): make this look nice on Windows and use it there as well.
class AvatarButtonThemedBorder : public views::Border {
 public:
  AvatarButtonThemedBorder() {}
  ~AvatarButtonThemedBorder() override {}

  void Paint(const views::View& view, gfx::Canvas* canvas) override {
    // Start with an outer dark stroke.
    cc::PaintFlags stroke_flags;
    stroke_flags.setStyle(cc::PaintFlags::kStroke_Style);
    // The colors are chosen to match the assets we use for Linux.
    stroke_flags.setColor(SkColorSetA(SK_ColorBLACK, 0x2B));
    stroke_flags.setStrokeWidth(kStrokeWidth);
    stroke_flags.setAntiAlias(true);
    gfx::RectF stroke_bounds(view.GetLocalBounds());
    stroke_bounds.Inset(gfx::InsetsF(0.5f));
    canvas->DrawRoundRect(stroke_bounds, kCornerRadius, stroke_flags);

    // There's a second, light stroke that matches the fill bounds.
    stroke_bounds.Inset(gfx::InsetsF(kStrokeWidth));
    stroke_flags.setColor(SkColorSetA(SK_ColorWHITE, 0x3F));
    canvas->DrawRoundRect(stroke_bounds, kCornerRadius, stroke_flags);
  }

  gfx::Insets GetInsets() const override {
    auto insets = views::LabelButtonAssetBorder::GetDefaultInsetsForStyle(
        views::Button::STYLE_TEXTBUTTON);
    return kBorderStrokeInsets +
           gfx::Insets(0, insets.left(), 0, insets.right());
  }

  gfx::Size GetMinimumSize() const override {
    return gfx::Size(GetInsets().width(), GetInsets().height());
  }

  static std::unique_ptr<views::InkDropMask> CreateInkDropMask(
      const gfx::Size& size) {
    return base::MakeUnique<views::RoundRectInkDropMask>(
        size, kBorderStrokeInsets, kCornerRadius);
  }

 private:
  static constexpr int kStrokeWidth = 1;

  // Insets between view bounds and the interior of the strokes.
  static constexpr gfx::Insets kBorderStrokeInsets{kStrokeWidth * 2};

  // Corner radius of the roundrect.
  static constexpr float kCornerRadius = 1;

  DISALLOW_COPY_AND_ASSIGN(AvatarButtonThemedBorder);
};

constexpr int AvatarButtonThemedBorder::kStrokeWidth;
constexpr gfx::Insets AvatarButtonThemedBorder::kBorderStrokeInsets;
constexpr float AvatarButtonThemedBorder::kCornerRadius;

}  // namespace

AvatarButton::AvatarButton(views::ButtonListener* listener,
                           AvatarButtonStyle button_style,
                           Profile* profile)
    : LabelButton(listener, base::string16()),
      error_controller_(this, profile),
      profile_(profile),
      profile_observer_(this),
      button_style_(button_style) {
  set_notify_action(CustomButton::NOTIFY_ON_PRESS);
  set_triggerable_event_flags(ui::EF_LEFT_MOUSE_BUTTON |
                              ui::EF_RIGHT_MOUSE_BUTTON);
  set_animate_on_state_change(false);
  SetEnabledTextColors(SK_ColorWHITE);
  SetTextSubpixelRenderingEnabled(false);
  SetHorizontalAlignment(gfx::ALIGN_CENTER);

  profile_observer_.Add(
      &g_browser_process->profile_manager()->GetProfileAttributesStorage());

  // The largest text height that fits in the button. If the font list height
  // is larger than this, it will be shrunk to match it.
  // TODO(noms): Calculate this constant algorithmically from the button's size.
  const int kDisplayFontHeight = 16;
  label()->SetFontList(
      label()->font_list().DeriveWithHeightUpperBound(kDisplayFontHeight));

  bool apply_ink_drop = IsCondensible();
#if defined(OS_LINUX)
  DCHECK_EQ(AvatarButtonStyle::THEMED, button_style);
  apply_ink_drop = true;
#endif

  if (apply_ink_drop) {
    constexpr int kIconSize = 16;
    constexpr SkColor kIconColor =
        SkColorSetA(SK_ColorBLACK, static_cast<SkAlpha>(0.54 * 0xFF));
    generic_avatar_ =
        gfx::CreateVectorIcon(kAccountCircleIcon, kIconSize, kIconColor);

    SetInkDropMode(InkDropMode::ON);
    SetFocusPainter(nullptr);
#if defined(OS_LINUX)
    set_ink_drop_base_color(SK_ColorWHITE);
    SetBorder(base::MakeUnique<AvatarButtonThemedBorder>());
#elif defined(OS_WIN)
    DCHECK_EQ(AvatarButtonStyle::NATIVE, button_style);
    set_ink_drop_base_color(SK_ColorBLACK);
    SetBorder(views::CreateEmptyBorder(kBorderInsets));
#endif  // defined(OS_WIN)
  } else if (button_style == AvatarButtonStyle::THEMED) {
    const int kNormalImageSet[] = IMAGE_GRID(IDR_AVATAR_THEMED_BUTTON_NORMAL);
    const int kHoverImageSet[] = IMAGE_GRID(IDR_AVATAR_THEMED_BUTTON_HOVER);
    const int kPressedImageSet[] = IMAGE_GRID(IDR_AVATAR_THEMED_BUTTON_PRESSED);
    SetButtonAvatar(IDR_AVATAR_THEMED_BUTTON_AVATAR);
    SetBorder(
        CreateThemedBorder(kNormalImageSet, kHoverImageSet, kPressedImageSet));
#if defined(OS_WIN)
  } else if (base::win::GetVersion() < base::win::VERSION_WIN8) {
    const int kNormalImageSet[] = IMAGE_GRID(IDR_AVATAR_GLASS_BUTTON_NORMAL);
    const int kHoverImageSet[] = IMAGE_GRID(IDR_AVATAR_GLASS_BUTTON_HOVER);
    const int kPressedImageSet[] = IMAGE_GRID(IDR_AVATAR_GLASS_BUTTON_PRESSED);
    SetButtonAvatar(IDR_AVATAR_GLASS_BUTTON_AVATAR);
    SetBorder(
        CreateThemedBorder(kNormalImageSet, kHoverImageSet, kPressedImageSet));
#endif
  } else {
    const int kNormalImageSet[] = IMAGE_GRID(IDR_AVATAR_NATIVE_BUTTON_NORMAL);
    const int kHoverImageSet[] = IMAGE_GRID(IDR_AVATAR_NATIVE_BUTTON_HOVER);
    const int kPressedImageSet[] = IMAGE_GRID(IDR_AVATAR_NATIVE_BUTTON_PRESSED);
    SetButtonAvatar(IDR_AVATAR_NATIVE_BUTTON_AVATAR);
    SetBorder(
        CreateThemedBorder(kNormalImageSet, kHoverImageSet, kPressedImageSet));
  }

  Update();
  SchedulePaint();
}

AvatarButton::~AvatarButton() {}

void AvatarButton::OnGestureEvent(ui::GestureEvent* event) {
  // TODO(wjmaclean): The check for ET_GESTURE_LONG_PRESS is done here since
  // no other UI button based on CustomButton appears to handle mouse
  // right-click. If other cases are identified, it may make sense to move this
  // check to CustomButton.
  if (event->type() == ui::ET_GESTURE_LONG_PRESS)
    NotifyClick(*event);
  else
    LabelButton::OnGestureEvent(event);
}

gfx::Size AvatarButton::GetMinimumSize() const {
  if (IsCondensible()) {
    // Returns the size of the button when it is atop the tabstrip. Called by
    // GlassBrowserFrameView::LayoutProfileSwitcher().
    // TODO(emx): Calculate the height based on the top of the new tab button.
    return gfx::Size(kCondensibleButtonMinWidth, 20);
  }

  return LabelButton::GetMinimumSize();
}

gfx::Size AvatarButton::GetPreferredSize() const {
  // TODO(estade): Calculate the height instead of hardcoding to 20 for the
  // not-condensible case.
  gfx::Size size(LabelButton::GetPreferredSize().width(), 20);

  if (IsCondensible()) {
    // Returns the normal size of the button (when it does not overlap the
    // tabstrip).
    size.set_width(std::min(std::max(size.width(), kCondensibleButtonMinWidth),
                            kCondensibleButtonMaxWidth));
#if defined(OS_WIN)
    size.set_height(MinimizeButtonMetrics::GetCaptionButtonHeightInDIPs());
#endif
  }

  return size;
}

std::unique_ptr<views::InkDropMask> AvatarButton::CreateInkDropMask() const {
  if (button_style_ == AvatarButtonStyle::THEMED)
    return AvatarButtonThemedBorder::CreateInkDropMask(size());
  return LabelButton::CreateInkDropMask();
}

std::unique_ptr<views::InkDropHighlight> AvatarButton::CreateInkDropHighlight()
    const {
  if (button_style_ == AvatarButtonStyle::THEMED)
    return LabelButton::CreateInkDropHighlight();

  auto ink_drop_highlight = base::MakeUnique<views::InkDropHighlight>(
      size(), 0, gfx::RectF(GetLocalBounds()).CenterPoint(),
      GetInkDropBaseColor());
  constexpr float kInkDropHighlightOpacity = 0.08f;
  ink_drop_highlight->set_visible_opacity(kInkDropHighlightOpacity);
  return ink_drop_highlight;
}

void AvatarButton::NotifyClick(const ui::Event& event) {
  LabelButton::NotifyClick(event);

  views::Widget* bubble_widget = ProfileChooserView::GetCurrentBubbleWidget();
  if (bubble_widget && !bubble_widget->HasObserver(this)) {
    ProfileChooserView::GetCurrentBubbleWidget()->AddObserver(this);
    AnimateInkDrop(views::InkDropState::ACTIVATED,
                   ui::LocatedEvent::FromIfValid(&event));
  }
}

bool AvatarButton::ShouldEnterPushedState(const ui::Event& event) {
  if (ProfileChooserView::IsShowing())
    return false;

  return LabelButton::ShouldEnterPushedState(event);
}

bool AvatarButton::ShouldUseFloodFillInkDrop() const {
  return true;
}

void AvatarButton::OnAvatarErrorChanged() {
  Update();
}

void AvatarButton::OnProfileAdded(const base::FilePath& profile_path) {
  Update();
}

void AvatarButton::OnProfileWasRemoved(const base::FilePath& profile_path,
                                       const base::string16& profile_name) {
  // If deleting the active profile, don't bother updating the avatar
  // button, as the browser window is being closed anyway.
  if (profile_->GetPath() != profile_path)
    Update();
}

void AvatarButton::OnProfileNameChanged(
    const base::FilePath& profile_path,
    const base::string16& old_profile_name) {
  if (profile_->GetPath() == profile_path)
    Update();
}

void AvatarButton::OnProfileSupervisedUserIdChanged(
    const base::FilePath& profile_path) {
  if (profile_->GetPath() == profile_path)
    Update();
}

void AvatarButton::OnWidgetClosing(views::Widget* widget) {
  AnimateInkDrop(views::InkDropState::DEACTIVATED, nullptr);
}

void AvatarButton::Update() {
  ProfileAttributesStorage& storage =
      g_browser_process->profile_manager()->GetProfileAttributesStorage();

  // If we have a single local profile, then use the generic avatar
  // button instead of the profile name. Never use the generic button if
  // the active profile is Guest.
  const bool use_generic_button =
      !profile_->IsGuestSession() && storage.GetNumberOfProfiles() == 1 &&
      !SigninManagerFactory::GetForProfile(profile_)->IsAuthenticated();

  SetText(use_generic_button
              ? base::string16()
              : profiles::GetAvatarButtonTextForProfile(profile_));

  // If the button has no text, clear the text shadows to make sure the
  // image is centered correctly.
  SetTextShadows(
      use_generic_button
          ? gfx::ShadowValues()
          : gfx::ShadowValues(
                10, gfx::ShadowValue(gfx::Vector2d(), 2.0f, SK_ColorDKGRAY)));

  // We want the button to resize if the new text is shorter.
  SetMinSize(gfx::Size());

  if (use_generic_button) {
    SetImage(views::Button::STATE_NORMAL, generic_avatar_);
  } else if (error_controller_.HasAvatarError()) {
    SetImage(views::Button::STATE_NORMAL,
             gfx::CreateVectorIcon(kSyncProblemIcon, 16, gfx::kGoogleRed700));
  } else {
    SetImage(views::Button::STATE_NORMAL, gfx::ImageSkia());
  }

  // If we are not using the generic button, then reset the spacing between
  // the text and the possible authentication error icon.
  const int kDefaultImageTextSpacing = 5;
  SetImageLabelSpacing(use_generic_button ? 0 : kDefaultImageTextSpacing);

  PreferredSizeChanged();
}

void AvatarButton::SetButtonAvatar(int avatar_idr) {
  ui::ResourceBundle* rb = &ui::ResourceBundle::GetSharedInstance();
  generic_avatar_ = *rb->GetImageNamed(avatar_idr).ToImageSkia();
}

// TODO(estade): all versions of this button should condense.
bool AvatarButton::IsCondensible() const {
#if defined(OS_WIN)
  return (base::win::GetVersion() >= base::win::VERSION_WIN10) &&
         button_style_ == AvatarButtonStyle::NATIVE;
#else
  return false;
#endif
}
