// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_TAB_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_TAB_H_

#include <list>
#include <memory>
#include <string>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "cc/paint/paint_record.h"
#include "chrome/browser/ui/views/tabs/tab_renderer_data.h"
#include "ui/base/layout.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/paint_throbber.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/glow_hover_controller.h"
#include "ui/views/masked_targeter_delegate.h"
#include "ui/views/view.h"

class AlertIndicatorButton;
class TabController;

namespace gfx {
class Animation;
class AnimationContainer;
class LinearAnimation;
class ThrobAnimation;
}
namespace views {
class ImageButton;
class Label;
}

///////////////////////////////////////////////////////////////////////////////
//
//  A View that renders a Tab in a TabStrip.
//
///////////////////////////////////////////////////////////////////////////////
class Tab : public gfx::AnimationDelegate,
            public views::ButtonListener,
            public views::ContextMenuController,
            public views::MaskedTargeterDelegate,
            public views::View {
 public:
  // The Tab's class name.
  static const char kViewClassName[];

  // The combined width of the curves at the top and bottom of the endcap.
  static constexpr float kMinimumEndcapWidth = 4;

  Tab(TabController* controller, gfx::AnimationContainer* container);
  ~Tab() override;

  TabController* controller() const { return controller_; }

  // Used to set/check whether this Tab is being animated closed.
  void set_closing(bool closing) { closing_ = closing; }
  bool closing() const { return closing_; }

  // See description above field.
  void set_dragging(bool dragging) { dragging_ = dragging; }
  bool dragging() const { return dragging_; }

  // Used to mark the tab as having been detached.  Once this has happened, the
  // tab should be invisibly closed.  This is irreversible.
  void set_detached() { detached_ = true; }
  bool detached() const { return detached_; }

  SkColor button_color() const { return button_color_; }

  // Returns true if this tab is the active tab.
  bool IsActive() const;

  // Notifies the AlertIndicatorButton that the active state of this tab has
  // changed.
  void ActiveStateChanged();

  // Called when the alert indicator has changed states.
  void AlertStateChanged();

  // Returns true if the tab is selected.
  bool IsSelected() const;

  // Sets the data this tabs displays. Invokes DataChanged. Should only be
  // called after Tab is added to widget hierarchy.
  void SetData(const TabRendererData& data);
  const TabRendererData& data() const { return data_; }

  // Sets the network state.
  void UpdateLoadingAnimation(TabRendererData::NetworkState state);

  // Starts/Stops a pulse animation.
  void StartPulse();
  void StopPulse();

  // Sets the visibility of the indicator shown when the tab title changes of
  // an inactive pinned tab.
  void SetPinnedTabTitleChangedIndicatorVisible(bool value);

  // Set the background offset used to match the image in the inactive tab
  // to the frame image.
  void set_background_offset(const gfx::Point& offset) {
    background_offset_ = offset;
  }

  // Returns true if this tab became the active tab selected in
  // response to the last ui::ET_TAP_DOWN gesture dispatched to
  // this tab. Only used for collecting UMA metrics.
  // See ash/touch/touch_uma.cc.
  bool tab_activated_with_last_tap_down() const {
    return tab_activated_with_last_tap_down_;
  }

  views::GlowHoverController* hover_controller() {
    return &hover_controller_;
  }

  // Returns the width of the largest part of the tab that is available for the
  // user to click to select/activate the tab.
  int GetWidthOfLargestSelectableRegion() const;

  // Called when stacked layout changes and the close button may need to
  // be updated.
  void HideCloseButtonForInactiveTabsChanged() { Layout(); }

  // Returns the minimum possible size of a single unselected Tab.
  static gfx::Size GetMinimumInactiveSize();

  // Returns the minimum possible size of a selected Tab. Selected tabs must
  // always show a close button and have a larger minimum size than unselected
  // tabs.
  static gfx::Size GetMinimumActiveSize();
  // Returns the preferred size of a single Tab, assuming space is
  // available.
  static gfx::Size GetStandardSize();

  // Returns the width for touch tabs.
  static int GetTouchWidth();

  // Returns the width for pinned tabs. Pinned tabs always have this width.
  static int GetPinnedWidth();

  // Returns the inverse of the slope of the diagonal portion of the tab outer
  // border.  (This is a positive value, so it's specifically for the slope of
  // the leading edge.)
  //
  // This returns the inverse (dx/dy instead of dy/dx) because we use exact
  // values for the vertical distances between points and then compute the
  // horizontal deltas from those.
  static float GetInverseDiagonalSlope();

  // Returns the overlap between adjacent tabs.
  static int GetOverlap();

 private:
  friend class AlertIndicatorButtonTest;
  friend class TabTest;
  friend class TabStripTest;
  FRIEND_TEST_ALL_PREFIXES(TabStripTest, TabCloseButtonVisibilityWhenStacked);

  // The animation object used to swap the favicon with the sad tab icon.
  class FaviconCrashAnimation;

  class TabCloseButton;
  class ThrobberView;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // views::ContextMenuController:
  void ShowContextMenuForView(views::View* source,
                              const gfx::Point& point,
                              ui::MenuSourceType source_type) override;

  // views::MaskedTargeterDelegate:
  bool GetHitTestMask(gfx::Path* mask) const override;

  // views::View:
  void ViewHierarchyChanged(
      const ViewHierarchyChangedDetails& details) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void Layout() override;
  void OnThemeChanged() override;
  const char* GetClassName() const override;
  bool GetTooltipText(const gfx::Point& p,
                      base::string16* tooltip) const override;
  bool GetTooltipTextOrigin(const gfx::Point& p,
                            gfx::Point* origin) const override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseCaptureLost() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override;

  // ui::EventHandler:
  void OnGestureEvent(ui::GestureEvent* event) override;

  // Invoked from Layout to adjust the position of the favicon or alert
  // indicator for pinned tabs.
  void MaybeAdjustLeftForPinnedTab(gfx::Rect* bounds) const;

  // Invoked from SetData after |data_| has been updated to the new data.
  void DataChanged(const TabRendererData& old);

  // Paints with the normal tab style.  If |clip| is non-empty, the tab border
  // should be clipped against it.
  void PaintTab(gfx::Canvas* canvas, const gfx::Path& clip);

  // Paints the background of an inactive tab.
  void PaintInactiveTabBackground(gfx::Canvas* canvas, const gfx::Path& clip);

  // Paints a tab background using the image defined by |fill_id| at the
  // provided offset. If |fill_id| is 0, it will fall back to using the solid
  // color defined by the theme provider and ignore the offset.
  void PaintTabBackground(gfx::Canvas* canvas,
                          bool active,
                          int fill_id,
                          int y_offset,
                          const gfx::Path* clip);

  // Helper methods for PaintTabBackground.
  void PaintTabBackgroundFill(gfx::Canvas* canvas,
                              const gfx::Path& fill_path,
                              bool active,
                              bool hover,
                              SkColor active_color,
                              SkColor inactive_color,
                              int fill_id,
                              int y_offset);
  void PaintTabBackgroundStroke(gfx::Canvas* canvas,
                                const gfx::Path& fill_path,
                                const gfx::Path& stroke_path,
                                bool active,
                                SkColor color);

  // Paints the pinned tab title changed indicator and |favicon_|. |favicon_|
  // may be null. |favicon_draw_bounds| is |favicon_bounds_| adjusted for rtl
  // and clipped to the bounds of the tab.
  void PaintPinnedTabTitleChangedIndicatorAndIcon(
      gfx::Canvas* canvas,
      const gfx::Rect& favicon_draw_bounds);

  // Paints the favicon, mirrored for RTL if needed.
  void PaintIcon(gfx::Canvas* canvas);

  // Invoked if data_.network_state changes, or the network_state is not none.
  void AdvanceLoadingAnimation();

  // Returns the number of favicon-size elements that can fit in the tab's
  // current size.
  int IconCapacity() const;

  // Returns whether the Tab should display a favicon.
  bool ShouldShowIcon() const;

  // Returns whether the Tab should display the alert indicator.
  bool ShouldShowAlertIndicator() const;

  // Returns whether the Tab should display a close button.
  bool ShouldShowCloseBox() const;

  // Returns whether the tab should be rendered as a normal tab as opposed to a
  // pinned tab.
  bool ShouldRenderAsNormalTab() const;

  // Gets the throb value for the tab. When a tab is not selected the
  // active background is drawn at |GetThrobValue()|%. This is used for hover,
  // mini tab title change and pulsing.
  double GetThrobValue();

  // Set the temporary offset for the favicon. This is used during the crash
  // animation.
  void SetFaviconHidingOffset(int offset);

  void SetShouldDisplayCrashedFavicon(bool value);

  // Recalculates the correct |button_color_| and resets the title, alert
  // indicator, and close button colors if necessary.  This should be called any
  // time the theme or active state may have changed.
  void OnButtonColorMaybeChanged();

  // Schedules repaint task for icon.
  void ScheduleIconPaint();

  // The controller, never NULL.
  TabController* const controller_;

  TabRendererData data_;

  // True if the tab is being animated closed.
  bool closing_;

  // True if the tab is being dragged.
  bool dragging_;

  // True if the tab has been detached.
  bool detached_;

  // The offset used to animate the favicon location. This is used when the tab
  // crashes.
  int favicon_hiding_offset_;

  bool should_display_crashed_favicon_;

  bool showing_pinned_tab_title_changed_indicator_ = false;

  // Whole-tab throbbing "pulse" animation.
  std::unique_ptr<gfx::ThrobAnimation> pulse_animation_;

  // Crash icon animation (in place of favicon).
  std::unique_ptr<gfx::LinearAnimation> crash_icon_animation_;

  scoped_refptr<gfx::AnimationContainer> animation_container_;

  ThrobberView* throbber_;
  AlertIndicatorButton* alert_indicator_button_;
  views::ImageButton* close_button_;
  views::Label* title_;

  bool tab_activated_with_last_tap_down_;

  views::GlowHoverController hover_controller_;

  // The bounds of various sections of the display.
  gfx::Rect favicon_bounds_;

  // The offset used to paint the inactive background image.
  gfx::Point background_offset_;

  // Whether we're showing the icon. It is cached so that we can detect when it
  // changes and layout appropriately.
  bool showing_icon_;

  // Whether we're showing the alert indicator. It is cached so that we can
  // detect when it changes and layout appropriately.
  bool showing_alert_indicator_;

  // Whether we are showing the close button. It is cached so that we can
  // detect when it changes and layout appropriately.
  bool showing_close_button_;

  // The current color of the alert indicator and close button icons.
  SkColor button_color_;

  // The favicon for the tab. This might be the sad tab icon or a copy of
  // data().favicon and may be modified for theming. It is created on demand
  // and thus may be null.
  gfx::ImageSkia favicon_;

  class BackgroundCache {
   public:
    BackgroundCache();
    ~BackgroundCache();

    bool CacheKeyMatches(float scale,
                         const gfx::Size& size,
                         SkColor active_color,
                         SkColor inactive_color,
                         SkColor stroke_color) {
      return scale_ == scale && size_ == size &&
             active_color_ == active_color &&
             inactive_color_ == inactive_color && stroke_color_ == stroke_color;
    }

    void SetCacheKey(float scale,
                     const gfx::Size& size,
                     SkColor active_color,
                     SkColor inactive_color,
                     SkColor stroke_color) {
      scale_ = scale;
      size_ = size;
      active_color_ = active_color;
      inactive_color_ = inactive_color;
      stroke_color_ = stroke_color;
    }

    // The PaintRecords being cached based on the input parameters.
    sk_sp<cc::PaintRecord> fill_record;
    sk_sp<cc::PaintRecord> stroke_record;

   private:
    // Parameters used to construct the PaintRecords.
    float scale_ = 0.f;
    gfx::Size size_;
    SkColor active_color_ = 0;
    SkColor inactive_color_ = 0;
    SkColor stroke_color_ = 0;
  };

  // Cache of the paint output for tab backgrounds.
  BackgroundCache background_active_cache_;
  BackgroundCache background_inactive_cache_;

  DISALLOW_COPY_AND_ASSIGN(Tab);
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_TAB_H_
