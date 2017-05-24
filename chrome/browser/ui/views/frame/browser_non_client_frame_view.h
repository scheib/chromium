// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_NON_CLIENT_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_NON_CLIENT_FRAME_VIEW_H_

#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/ui/views/profiles/profile_indicator_icon.h"
#include "ui/views/window/non_client_view.h"

class BrowserFrame;
class BrowserView;

// A specialization of the NonClientFrameView object that provides additional
// Browser-specific methods.
class BrowserNonClientFrameView : public views::NonClientFrameView,
                                  public ProfileAttributesStorage::Observer {
 public:
  // The padding on the left, right, and bottom of the avatar icon.
  static constexpr int kAvatarIconPadding = 4;

  BrowserNonClientFrameView(BrowserFrame* frame, BrowserView* browser_view);
  ~BrowserNonClientFrameView() override;

  BrowserView* browser_view() const { return browser_view_; }
  BrowserFrame* frame() const { return frame_; }

  // Called when BrowserView creates all it's child views.
  virtual void OnBrowserViewInitViewsComplete();

  // Retrieves the bounds, in non-client view coordinates within which the
  // TabStrip should be laid out.
  virtual gfx::Rect GetBoundsForTabStrip(views::View* tabstrip) const = 0;

  // Returns the inset of the topmost view in the client view from the top of
  // the non-client view. The topmost view depends on the window type. The
  // topmost view is the tab strip for tabbed browser windows, the toolbar for
  // popups, the web contents for app windows and varies for fullscreen windows.
  // If |restored| is true, this is calculated as if the window was restored,
  // regardless of its current state.
  virtual int GetTopInset(bool restored) const = 0;

  // Returns the amount that the theme background should be inset.
  virtual int GetThemeBackgroundXInset() const = 0;

  // Retrieves the icon to use in the frame to indicate an incognito window.
  gfx::ImageSkia GetIncognitoAvatarIcon() const;

  // Returns COLOR_TOOLBAR_TOP_SEPARATOR[,_INACTIVE] depending on the activation
  // state of the window.
  SkColor GetToolbarTopSeparatorColor() const;

  // Updates the throbber.
  virtual void UpdateThrobber(bool running) = 0;

  // Returns the profile switcher button, if this frame has any.
  virtual views::View* GetProfileSwitcherView() const;

  // Provided for mus. Updates the client-area of the WindowTreeHostMus.
  virtual void UpdateClientArea();

  // Overriden from views::View.
  void ChildPreferredSizeChanged(views::View* child) override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;

 protected:
  // Whether the frame should be painted with theming.
  // By default, tabbed browser windows are themed but popup and app windows are
  // not.
  virtual bool ShouldPaintAsThemed() const;

  // Compute aspects of the frame needed to paint the frame background.
  SkColor GetFrameColor(bool active) const;
  gfx::ImageSkia GetFrameImage(bool active) const;
  gfx::ImageSkia GetFrameOverlayImage(bool active) const;

  // Convenience versions of the above which use ShouldPaintAsActive() for
  // |active|.
  SkColor GetFrameColor() const;
  gfx::ImageSkia GetFrameImage() const;
  gfx::ImageSkia GetFrameOverlayImage() const;

  // Updates the profile switcher button if one should exist. Otherwise, updates
  // the icon that indicates incognito (or a teleported window in ChromeOS).
  virtual void UpdateProfileIcons() = 0;

  // Updates the icon that indicates incognito/teleportation state.
  void UpdateProfileIndicatorIcon();

  const views::View* profile_indicator_icon() const {
    return profile_indicator_icon_;
  }
  views::View* profile_indicator_icon() {
    return profile_indicator_icon_;
  }

  // views::NonClientFrameView:
  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override;

 private:
  // views::NonClientFrameView:
  void ViewHierarchyChanged(
      const ViewHierarchyChangedDetails& details) override;
  void ActivationChanged(bool active) override;

  // ProfileAttributesStorage::Observer:
  void OnProfileAdded(const base::FilePath& profile_path) override;
  void OnProfileWasRemoved(const base::FilePath& profile_path,
                           const base::string16& profile_name) override;
  void OnProfileAvatarChanged(const base::FilePath& profile_path) override;

  // Gets a theme provider that should be non-null even before we're added to a
  // view hierarchy.
  const ui::ThemeProvider* GetThemeProviderForProfile() const;

  // Draws a taskbar icon if avatars are enabled, erases it otherwise.
  void UpdateTaskbarDecoration();

  // The frame that hosts this view.
  BrowserFrame* frame_;

  // The BrowserView hosted within this View.
  BrowserView* browser_view_;

  // On desktop, this is used to show an incognito icon. On CrOS, it's also used
  // for teleported windows (in multi-profile mode).
  ProfileIndicatorIcon* profile_indicator_icon_;

  DISALLOW_COPY_AND_ASSIGN(BrowserNonClientFrameView);
};

namespace chrome {

// Provided by a browser_non_client_frame_view_factory_*.cc implementation
BrowserNonClientFrameView* CreateBrowserNonClientFrameView(
    BrowserFrame* frame, BrowserView* browser_view);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_NON_CLIENT_FRAME_VIEW_H_
