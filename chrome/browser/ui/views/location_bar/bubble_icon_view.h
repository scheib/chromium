// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_BUBBLE_ICON_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_BUBBLE_ICON_VIEW_H_

#include <memory>

#include "base/macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/animation/ink_drop_host_view.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/widget/widget_observer.h"

class CommandUpdater;

namespace gfx {
struct VectorIcon;
}

namespace views {
class BubbleDialogDelegateView;
}

// Represents an icon on the omnibox that shows a bubble when clicked.
// TODO(spqchan): Convert this to subclass CustomButton.
class BubbleIconView : public views::InkDropHostView,
                       public views::WidgetObserver {
 public:
  void Init();

 protected:
  enum ExecuteSource {
    EXECUTE_SOURCE_MOUSE,
    EXECUTE_SOURCE_KEYBOARD,
    EXECUTE_SOURCE_GESTURE,
  };

  BubbleIconView(CommandUpdater* command_updater, int command_id);
  ~BubbleIconView() override;

  // Returns true if a related bubble is showing.
  bool IsBubbleShowing() const;

  // Sets the image that should be displayed in |image_|.
  void SetImage(const gfx::ImageSkia* image_skia);

  // Returns the image currently displayed, which can be empty if not set.
  // The returned image is owned by |image_|.
  const gfx::ImageSkia& GetImage() const;

  // Sets the tooltip text.
  void SetTooltipText(const base::string16& tooltip);

  // Invoked prior to executing the command.
  virtual void OnExecuting(ExecuteSource execute_source) = 0;

  // Invoked after the icon is pressed.
  virtual void OnPressed(bool activated) {}

  // views::View:
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override;
  bool GetTooltipText(const gfx::Point& p,
                      base::string16* tooltip) const override;
  gfx::Size GetPreferredSize() const override;
  void Layout() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  bool OnKeyReleased(const ui::KeyEvent& event) override;
  void ViewHierarchyChanged(
      const ViewHierarchyChangedDetails& details) override;
  void OnNativeThemeChanged(const ui::NativeTheme* theme) override;
  void AddInkDropLayer(ui::Layer* ink_drop_layer) override;
  void RemoveInkDropLayer(ui::Layer* ink_drop_layer) override;
  std::unique_ptr<views::InkDrop> CreateInkDrop() override;
  std::unique_ptr<views::InkDropRipple> CreateInkDropRipple() const override;
  SkColor GetInkDropBaseColor() const override;

  // ui::EventHandler:
  void OnGestureEvent(ui::GestureEvent* event) override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

 protected:
  // Calls OnExecuting and runs |command_id_| with a valid |command_updater_|.
  virtual void ExecuteCommand(ExecuteSource source);

  // Returns the bubble instance for the icon.
  virtual views::BubbleDialogDelegateView* GetBubble() const = 0;

  // Gets the given vector icon in the correct color and size based on |active|.
  virtual const gfx::VectorIcon& GetVectorIcon() const = 0;

  // views::View:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

  // Updates the icon after some state has changed.
  void UpdateIcon();

  // Sets the active state of the icon. An active icon will be displayed in a
  // "call to action" color.
  void SetActiveInternal(bool active);

  bool active() const { return active_; }

 private:
  // The image shown in the button.
  views::ImageView* image_;

  // The CommandUpdater for the Browser object that owns the location bar.
  CommandUpdater* command_updater_;

  // The command ID executed when the user clicks this icon.
  const int command_id_;

  // The active node_data. The precise definition of "active" is unique to each
  // subclass, but generally indicates that the associated feature is acting on
  // the web page.
  bool active_;

  // This is used to check if the bookmark bubble was showing during the mouse
  // pressed event. If this is true then the mouse released event is ignored to
  // prevent the bubble from reshowing.
  bool suppress_mouse_released_action_;

  DISALLOW_COPY_AND_ASSIGN(BubbleIconView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_BUBBLE_ICON_VIEW_H_
