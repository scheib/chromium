// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/magnifier/magnification_controller.h"

#include "ash/accessibility_types.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/strings/stringprintf.h"
#include "ui/aura/env.h"
#include "ui/aura/test/aura_test_utils.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ime/input_method.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/screen.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace ash {
namespace {

const int kRootHeight = 600;
const int kRootWidth = 800;

const int kTextInputWindowWidth = 50;
const int kTextInputWindowHeight = 50;

class TextInputView : public views::WidgetDelegateView {
 public:
  TextInputView() : text_field_(new views::Textfield) {
    text_field_->SetTextInputType(ui::TEXT_INPUT_TYPE_TEXT);
    AddChildView(text_field_);
    SetLayoutManager(new views::FillLayout);
  }

  ~TextInputView() override {}

  gfx::Size CalculatePreferredSize() const override {
    return gfx::Size(kTextInputWindowWidth, kTextInputWindowHeight);
  }

  void FocusOnTextInput() { text_field_->RequestFocus(); }

 private:
  views::Textfield* text_field_;  // owned by views hierarchy

  DISALLOW_COPY_AND_ASSIGN(TextInputView);
};

}  // namespace

class MagnificationControllerTest : public test::AshTestBase {
 public:
  MagnificationControllerTest() : text_input_view_(NULL) {}
  ~MagnificationControllerTest() override {}

  void SetUp() override {
    AshTestBase::SetUp();
    UpdateDisplay(base::StringPrintf("%dx%d", kRootWidth, kRootHeight));

    GetMagnificationController()->DisableMoveMagnifierDelayForTesting();
  }

  void TearDown() override { AshTestBase::TearDown(); }

 protected:
  aura::Window* GetRootWindow() const { return Shell::GetPrimaryRootWindow(); }

  std::string GetHostMouseLocation() {
    const gfx::Point& location =
        aura::test::QueryLatestMousePositionRequestInHost(
            GetRootWindow()->GetHost());
    return location.ToString();
  }

  ash::MagnificationController* GetMagnificationController() const {
    return ash::Shell::Get()->magnification_controller();
  }

  gfx::Rect GetViewport() const {
    gfx::RectF bounds(0, 0, kRootWidth, kRootHeight);
    GetRootWindow()->layer()->transform().TransformRectReverse(&bounds);
    return gfx::ToEnclosingRect(bounds);
  }

  std::string CurrentPointOfInterest() const {
    return GetMagnificationController()
        ->GetPointOfInterestForTesting()
        .ToString();
  }

  void CreateAndShowTextInputView(const gfx::Rect& bounds) {
    text_input_view_ = new TextInputView;
    views::Widget* widget = views::Widget::CreateWindowWithContextAndBounds(
        text_input_view_, GetRootWindow(), bounds);
    widget->Show();
  }

  // Returns the text input view's bounds in root window coordinates.
  gfx::Rect GetTextInputViewBounds() {
    DCHECK(text_input_view_);
    gfx::Rect bounds = text_input_view_->bounds();
    gfx::Point origin = bounds.origin();
    // Convert origin to screen coordinates.
    views::View::ConvertPointToScreen(text_input_view_, &origin);
    // Convert origin to root_window_ coordinates.
    ::wm::ConvertPointFromScreen(GetRootWindow(), &origin);
    return gfx::Rect(origin.x(), origin.y(), bounds.width(), bounds.height());
  }

  // Returns the caret bounds in root window coordinates.
  gfx::Rect GetCaretBounds() {
    gfx::Rect caret_bounds =
        GetInputMethod()->GetTextInputClient()->GetCaretBounds();
    gfx::Point origin = caret_bounds.origin();
    ::wm::ConvertPointFromScreen(GetRootWindow(), &origin);
    return gfx::Rect(origin.x(), origin.y(), caret_bounds.width(),
                     caret_bounds.height());
  }

  void FocusOnTextInputView() {
    DCHECK(text_input_view_);
    text_input_view_->FocusOnTextInput();
  }

 private:
  TextInputView* text_input_view_;

  ui::InputMethod* GetInputMethod() {
    DCHECK(text_input_view_);
    return text_input_view_->GetWidget()->GetInputMethod();
  }

  DISALLOW_COPY_AND_ASSIGN(MagnificationControllerTest);
};

TEST_F(MagnificationControllerTest, EnableAndDisable) {
  // Confirms the magnifier is disabled.
  EXPECT_TRUE(GetRootWindow()->layer()->transform().IsIdentity());
  EXPECT_EQ(1.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());

  // Enables magnifier.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_FALSE(GetRootWindow()->layer()->transform().IsIdentity());
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());

  // Disables magnifier.
  GetMagnificationController()->SetEnabled(false);
  EXPECT_TRUE(GetRootWindow()->layer()->transform().IsIdentity());
  EXPECT_EQ(1.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());

  // Confirms the the scale can't be changed.
  GetMagnificationController()->SetScale(4.0f, false);
  EXPECT_TRUE(GetRootWindow()->layer()->transform().IsIdentity());
  EXPECT_EQ(1.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());
}

TEST_F(MagnificationControllerTest, MagnifyAndUnmagnify) {
  // Enables magnifier and confirms the default scale is 2.0x.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_FALSE(GetRootWindow()->layer()->transform().IsIdentity());
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());
  EXPECT_EQ("400,300", CurrentPointOfInterest());

  // Changes the scale.
  GetMagnificationController()->SetScale(4.0f, false);
  EXPECT_EQ(4.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("300,225 200x150", GetViewport().ToString());
  EXPECT_EQ("400,300", CurrentPointOfInterest());

  GetMagnificationController()->SetScale(1.0f, false);
  EXPECT_EQ(1.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());
  EXPECT_EQ("400,300", CurrentPointOfInterest());

  GetMagnificationController()->SetScale(3.0f, false);
  EXPECT_EQ(3.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("266,200 267x200", GetViewport().ToString());
  EXPECT_EQ("400,300", CurrentPointOfInterest());
}

TEST_F(MagnificationControllerTest, MoveWindow) {
  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());

  // Move the viewport.
  GetMagnificationController()->MoveWindow(0, 0, false);
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(200, 300, false);
  EXPECT_EQ("200,300 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(400, 0, false);
  EXPECT_EQ("400,0 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(400, 300, false);
  EXPECT_EQ("400,300 400x300", GetViewport().ToString());

  // Confirms that the viewport can't across the top-left border.
  GetMagnificationController()->MoveWindow(-100, 0, false);
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(0, -100, false);
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(-100, -100, false);
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  // Confirms that the viewport can't across the bittom-right border.
  GetMagnificationController()->MoveWindow(800, 0, false);
  EXPECT_EQ("400,0 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(0, 400, false);
  EXPECT_EQ("0,300 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(200, 400, false);
  EXPECT_EQ("200,300 400x300", GetViewport().ToString());

  GetMagnificationController()->MoveWindow(1000, 1000, false);
  EXPECT_EQ("400,300 400x300", GetViewport().ToString());
}

TEST_F(MagnificationControllerTest, PointOfInterest) {
  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 0));
  EXPECT_EQ("0,0", CurrentPointOfInterest());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 599));
  EXPECT_EQ("799,599", CurrentPointOfInterest());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(400, 300));
  EXPECT_EQ("400,300", CurrentPointOfInterest());

  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ("400,300", CurrentPointOfInterest());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(500, 400));
  EXPECT_EQ("450,350", CurrentPointOfInterest());
}

TEST_F(MagnificationControllerTest, FollowFocusChanged) {
  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());

  // Don't move viewport when focusing edit box.
  GetMagnificationController()->HandleFocusedNodeChanged(
      true, gfx::Rect(0, 0, 10, 10));
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());

  // Move viewport to element in upper left.
  GetMagnificationController()->HandleFocusedNodeChanged(
      false, gfx::Rect(0, 0, 10, 10));
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  // Move viewport to element in lower right.
  GetMagnificationController()->HandleFocusedNodeChanged(
      false, gfx::Rect(790, 590, 10, 10));
  EXPECT_EQ("400,300 400x300", GetViewport().ToString());

  // Don't follow focus onto empty rectangle.
  GetMagnificationController()->HandleFocusedNodeChanged(false,
                                                         gfx::Rect(0, 0, 0, 0));
  EXPECT_EQ("400,300 400x300", GetViewport().ToString());
}

TEST_F(MagnificationControllerTest, PanWindow2xLeftToRight) {
  const aura::Env* env = aura::Env::GetInstance();

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 0));
  EXPECT_EQ(1.f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());
  EXPECT_EQ("0,0", env->last_mouse_location().ToString());

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());

  GetMagnificationController()->MoveWindow(0, 0, false);
  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 0));
  EXPECT_EQ("0,0", env->last_mouse_location().ToString());
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(300, 150));
  EXPECT_EQ("150,75", env->last_mouse_location().ToString());
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(700, 150));
  EXPECT_EQ("350,75", env->last_mouse_location().ToString());
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(701, 150));
  EXPECT_EQ("350,75", env->last_mouse_location().ToString());
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(702, 150));
  EXPECT_EQ("351,75", env->last_mouse_location().ToString());
  EXPECT_EQ("1,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(703, 150));
  EXPECT_EQ("352,75", env->last_mouse_location().ToString());
  EXPECT_EQ("2,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(704, 150));
  EXPECT_EQ("354,75", env->last_mouse_location().ToString());
  EXPECT_EQ("4,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(712, 150));
  EXPECT_EQ("360,75", env->last_mouse_location().ToString());
  EXPECT_EQ("10,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(600, 150));
  EXPECT_EQ("310,75", env->last_mouse_location().ToString());
  EXPECT_EQ("10,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(720, 150));
  EXPECT_EQ("370,75", env->last_mouse_location().ToString());
  EXPECT_EQ("20,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("410,75", env->last_mouse_location().ToString());
  EXPECT_EQ("410,75", CurrentPointOfInterest());
  EXPECT_EQ("60,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 150));
  EXPECT_EQ("459,75", env->last_mouse_location().ToString());
  EXPECT_EQ("109,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(702, 150));
  EXPECT_EQ("460,75", env->last_mouse_location().ToString());
  EXPECT_EQ("110,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("500,75", env->last_mouse_location().ToString());
  EXPECT_EQ("150,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("540,75", env->last_mouse_location().ToString());
  EXPECT_EQ("190,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("580,75", env->last_mouse_location().ToString());
  EXPECT_EQ("230,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("620,75", env->last_mouse_location().ToString());
  EXPECT_EQ("270,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("660,75", env->last_mouse_location().ToString());
  EXPECT_EQ("310,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("700,75", env->last_mouse_location().ToString());
  EXPECT_EQ("350,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("740,75", env->last_mouse_location().ToString());
  EXPECT_EQ("390,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(780, 150));
  EXPECT_EQ("780,75", env->last_mouse_location().ToString());
  EXPECT_EQ("400,0 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 150));
  EXPECT_EQ("799,75", env->last_mouse_location().ToString());
  EXPECT_EQ("400,0 400x300", GetViewport().ToString());
}

TEST_F(MagnificationControllerTest, PanWindow2xRightToLeft) {
  const aura::Env* env = aura::Env::GetInstance();

  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 300));
  EXPECT_EQ(1.f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());
  EXPECT_EQ("799,300", env->last_mouse_location().ToString());

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);

  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 300));
  EXPECT_EQ("798,300", env->last_mouse_location().ToString());
  EXPECT_EQ("400,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("400,300", env->last_mouse_location().ToString());
  EXPECT_EQ("350,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("350,300", env->last_mouse_location().ToString());
  EXPECT_EQ("300,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("300,300", env->last_mouse_location().ToString());
  EXPECT_EQ("250,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("250,300", env->last_mouse_location().ToString());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("200,300", env->last_mouse_location().ToString());
  EXPECT_EQ("150,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("150,300", env->last_mouse_location().ToString());
  EXPECT_EQ("100,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("100,300", env->last_mouse_location().ToString());
  EXPECT_EQ("50,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("50,300", env->last_mouse_location().ToString());
  EXPECT_EQ("0,150 400x300", GetViewport().ToString());

  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("0,300", env->last_mouse_location().ToString());
  EXPECT_EQ("0,150 400x300", GetViewport().ToString());
}

TEST_F(MagnificationControllerTest, PanWindowToRight) {
  const aura::Env* env = aura::Env::GetInstance();

  GetEventGenerator().MoveMouseToInHost(gfx::Point(400, 300));
  EXPECT_EQ(1.f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());
  EXPECT_EQ("400,300", env->last_mouse_location().ToString());

  float scale = 2.f;

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_FLOAT_EQ(2.f, GetMagnificationController()->GetScale());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(2.3784142, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(400, 300));
  EXPECT_EQ("400,300", env->last_mouse_location().ToString());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 300));
  EXPECT_EQ("566,299", env->last_mouse_location().ToString());
  EXPECT_EQ("705,300", GetHostMouseLocation());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(2.8284268, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 300));
  EXPECT_EQ("599,299", env->last_mouse_location().ToString());
  EXPECT_EQ("702,300", GetHostMouseLocation());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(3.3635852, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 300));
  EXPECT_EQ("627,298", env->last_mouse_location().ToString());
  EXPECT_EQ("707,300", GetHostMouseLocation());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(4.f, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(799, 300));
  EXPECT_EQ("649,298", env->last_mouse_location().ToString());
  EXPECT_EQ("704,300", GetHostMouseLocation());
}

TEST_F(MagnificationControllerTest, PanWindowToLeft) {
  const aura::Env* env = aura::Env::GetInstance();

  GetEventGenerator().MoveMouseToInHost(gfx::Point(400, 300));
  EXPECT_EQ(1.f, GetMagnificationController()->GetScale());
  EXPECT_EQ("0,0 800x600", GetViewport().ToString());
  EXPECT_EQ("400,300", env->last_mouse_location().ToString());

  float scale = 2.f;

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_FLOAT_EQ(2.f, GetMagnificationController()->GetScale());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(2.3784142, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(400, 300));
  EXPECT_EQ("400,300", env->last_mouse_location().ToString());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("231,299", env->last_mouse_location().ToString());
  EXPECT_EQ("100,300", GetHostMouseLocation());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(2.8284268, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("194,299", env->last_mouse_location().ToString());
  EXPECT_EQ("99,300", GetHostMouseLocation());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(3.3635852, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("164,298", env->last_mouse_location().ToString());
  EXPECT_EQ("98,300", GetHostMouseLocation());

  scale *= kMagnificationScaleFactor;
  GetMagnificationController()->SetScale(scale, false);
  EXPECT_FLOAT_EQ(4.f, GetMagnificationController()->GetScale());
  GetEventGenerator().MoveMouseToInHost(gfx::Point(0, 300));
  EXPECT_EQ("139,298", env->last_mouse_location().ToString());
  EXPECT_EQ("100,300", GetHostMouseLocation());
}

TEST_F(MagnificationControllerTest, FollowTextInputFieldFocus) {
  CreateAndShowTextInputView(gfx::Rect(500, 300, 80, 80));
  gfx::Rect text_input_bounds = GetTextInputViewBounds();

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());
  EXPECT_FALSE(GetMagnificationController()->KeepFocusCentered());

  // Move the viewport to (0, 0), so that text input field will be out of
  // the viewport region.
  GetMagnificationController()->MoveWindow(0, 0, false);
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());
  EXPECT_FALSE(GetViewport().Intersects(text_input_bounds));

  // Focus on the text input field.
  FocusOnTextInputView();

  // Verify the view port has been moved to the place where the text field is
  // contained in the view port and the caret is at the center of the view port.
  gfx::Rect view_port = GetViewport();
  EXPECT_TRUE(view_port.Contains(text_input_bounds));
  gfx::Rect caret_bounds = GetCaretBounds();
  EXPECT_TRUE(text_input_bounds.Contains(caret_bounds));
  EXPECT_EQ(caret_bounds.CenterPoint(), view_port.CenterPoint());
}

// Tests the following case. First the text input field intersects on the right
// edge with the view port, with focus caret sitting just a little left to the
// caret panning margin, so that when it gets focus, the view port won't move.
// Then when user types a character, the caret moves beyond the right panning
// edge, the view port will be moved to center the caret horizontally.
TEST_F(MagnificationControllerTest, FollowTextInputFieldKeyPress) {
  const int kCaretPanningMargin = 50;
  const int kScale = 2.0f;
  const int kViewportWidth = 400;
  // Add some extra distance horizontally from text caret to to left edge of
  // the text input view.
  int x = kViewportWidth - (kCaretPanningMargin + 20) / kScale;
  CreateAndShowTextInputView(gfx::Rect(x, 200, 80, 80));
  gfx::Rect text_input_bounds = GetTextInputViewBounds();

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());
  EXPECT_FALSE(GetMagnificationController()->KeepFocusCentered());

  // Move the viewport to (0, 0), so that text input field intersects the
  // view port at the right edge.
  GetMagnificationController()->MoveWindow(0, 0, false);
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());
  EXPECT_TRUE(GetViewport().Intersects(text_input_bounds));

  // Focus on the text input field.
  FocusOnTextInputView();

  // Verify the view port is not moved, and the caret is inside the view port
  // and not beyond the caret right panning margin.
  gfx::Rect view_port = GetViewport();
  EXPECT_EQ("0,0 400x300", view_port.ToString());
  EXPECT_TRUE(text_input_bounds.Contains(GetCaretBounds()));
  EXPECT_GT(view_port.right() - kCaretPanningMargin / kScale,
            GetCaretBounds().x());

  // Press keys on text input simulate typing on text field and the caret
  // moves beyond the caret right panning margin. The view port is moved to the
  // place where caret's x coordinate is centered at the new view port.
  GetEventGenerator().PressKey(ui::VKEY_A, 0);
  GetEventGenerator().ReleaseKey(ui::VKEY_A, 0);
  gfx::Rect caret_bounds = GetCaretBounds();
  EXPECT_LT(view_port.right() - kCaretPanningMargin / kScale,
            GetCaretBounds().x());

  gfx::Rect new_view_port = GetViewport();
  EXPECT_EQ(caret_bounds.CenterPoint().x(), new_view_port.CenterPoint().x());
}

TEST_F(MagnificationControllerTest, CenterTextCaretNotInsideViewport) {
  CreateAndShowTextInputView(gfx::Rect(500, 300, 50, 30));
  gfx::Rect text_input_bounds = GetTextInputViewBounds();

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetKeepFocusCentered(true);
  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());
  EXPECT_TRUE(GetMagnificationController()->KeepFocusCentered());

  // Move the viewport to (0, 0), so that text input field will be out of
  // the viewport region.
  GetMagnificationController()->MoveWindow(0, 0, false);
  EXPECT_EQ("0,0 400x300", GetViewport().ToString());
  EXPECT_FALSE(GetViewport().Contains(text_input_bounds));

  // Focus on the text input field.
  FocusOnTextInputView();
  RunAllPendingInMessageLoop();
  // Verify the view port has been moved to the place where the text field is
  // contained in the view port and the caret is at the center of the view port.
  gfx::Rect view_port = GetViewport();
  EXPECT_TRUE(view_port.Contains(text_input_bounds));
  gfx::Rect caret_bounds = GetCaretBounds();
  EXPECT_EQ(caret_bounds.CenterPoint(), view_port.CenterPoint());

  // Press keys on text input simulate typing on text field and the view port
  // should be moved to keep the caret centered.
  GetEventGenerator().PressKey(ui::VKEY_A, 0);
  GetEventGenerator().ReleaseKey(ui::VKEY_A, 0);
  RunAllPendingInMessageLoop();
  gfx::Rect new_caret_bounds = GetCaretBounds();
  EXPECT_NE(caret_bounds, new_caret_bounds);

  gfx::Rect new_view_port = GetViewport();
  EXPECT_NE(view_port, new_view_port);
  EXPECT_TRUE(new_view_port.Contains(new_caret_bounds));
  EXPECT_EQ(new_caret_bounds.CenterPoint(), new_view_port.CenterPoint());
}

TEST_F(MagnificationControllerTest, CenterTextCaretInViewport) {
  CreateAndShowTextInputView(gfx::Rect(250, 200, 50, 30));
  gfx::Rect text_input_bounds = GetTextInputViewBounds();

  // Enables magnifier and confirm the viewport is at center.
  GetMagnificationController()->SetKeepFocusCentered(true);
  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());
  EXPECT_EQ("200,150 400x300", GetViewport().ToString());
  EXPECT_TRUE(GetMagnificationController()->KeepFocusCentered());

  // Verify the text input field is inside the view port.
  gfx::Rect view_port = GetViewport();
  EXPECT_TRUE(view_port.Contains(text_input_bounds));

  // Focus on the text input field.
  FocusOnTextInputView();
  RunAllPendingInMessageLoop();

  // Verify the view port has been moved to the place where the text field is
  // contained in the view port and the caret is at the center of the view port.
  gfx::Rect new_view_port = GetViewport();
  EXPECT_NE(view_port, new_view_port);
  EXPECT_TRUE(new_view_port.Contains(text_input_bounds));
  gfx::Rect caret_bounds = GetCaretBounds();
  EXPECT_EQ(caret_bounds.CenterPoint(), new_view_port.CenterPoint());
}

// Make sure that unified desktop can enter magnified mode.
TEST_F(MagnificationControllerTest, EnableMagnifierInUnifiedDesktop) {
  Shell::Get()->display_manager()->SetUnifiedDesktopEnabled(true);

  EXPECT_EQ(1.0f, GetMagnificationController()->GetScale());

  GetMagnificationController()->SetEnabled(true);

  display::Screen* screen = display::Screen::GetScreen();

  UpdateDisplay("500x500, 500x500");
  EXPECT_EQ("0,0 1000x500", screen->GetPrimaryDisplay().bounds().ToString());
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());

  GetMagnificationController()->SetEnabled(false);

  EXPECT_EQ(1.0f, GetMagnificationController()->GetScale());

  GetMagnificationController()->SetEnabled(true);
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());

  UpdateDisplay("500x500");
  EXPECT_EQ("0,0 500x500", screen->GetPrimaryDisplay().bounds().ToString());
  EXPECT_EQ(2.0f, GetMagnificationController()->GetScale());

  GetMagnificationController()->SetEnabled(false);
  EXPECT_EQ("0,0 500x500", screen->GetPrimaryDisplay().bounds().ToString());
  EXPECT_EQ(1.0f, GetMagnificationController()->GetScale());
}

// Make sure that mouse can move across display in magnified mode.
TEST_F(MagnificationControllerTest, MoveMouseToSecondDisplay) {
  UpdateDisplay("0+0-500x500, 500+0-500x500");
  EXPECT_EQ(2ul, display::Screen::GetScreen()->GetAllDisplays().size());

  aura::Window::Windows root_windows = Shell::GetAllRootWindows();

  GetEventGenerator().MoveMouseTo(gfx::Point(250, 250));
  EXPECT_TRUE(root_windows[1]->layer()->transform().IsIdentity());
  EXPECT_TRUE(root_windows[0]->layer()->transform().IsIdentity());

  GetMagnificationController()->SetEnabled(true);
  EXPECT_FALSE(root_windows[0]->layer()->transform().IsIdentity());
  EXPECT_TRUE(root_windows[1]->layer()->transform().IsIdentity());

  GetEventGenerator().MoveMouseTo(gfx::Point(750, 250));
  EXPECT_FALSE(root_windows[1]->layer()->transform().IsIdentity());
  EXPECT_TRUE(root_windows[0]->layer()->transform().IsIdentity());

  GetMagnificationController()->SetEnabled(false);
  EXPECT_TRUE(root_windows[1]->layer()->transform().IsIdentity());
  EXPECT_TRUE(root_windows[0]->layer()->transform().IsIdentity());
}

}  // namespace ash
