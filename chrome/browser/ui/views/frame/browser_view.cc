// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_view.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/profiler/scoped_tracker.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/bookmarks/bookmark_stats.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/native_window_notification_source.h"
#include "chrome/browser/permissions/permission_request_manager.h"
#include "chrome/browser/profiles/avatar_menu.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/signin/chrome_signin_helper.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/bookmarks/bookmark_bar_constants.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window_state.h"
#include "chrome/browser/ui/sync/bubble_sync_promo_delegate.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/accelerator_table.h"
#include "chrome/browser/ui/views/accessibility/invert_bubble_view.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bubble_view.h"
#include "chrome/browser/ui/views/download/download_in_progress_dialog_view.h"
#include "chrome/browser/ui/views/download/download_shelf_view.h"
#include "chrome/browser/ui/views/exclusive_access_bubble_views.h"
#include "chrome/browser/ui/views/extensions/bookmark_app_confirmation_view.h"
#include "chrome/browser/ui/views/extensions/extension_keybinding_registry_views.h"
#include "chrome/browser/ui/views/find_bar_host.h"
#include "chrome/browser/ui/views/frame/browser_view_layout.h"
#include "chrome/browser/ui/views/frame/browser_view_layout_delegate.h"
#include "chrome/browser/ui/views/frame/contents_layout_manager.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/web_contents_close_handler.h"
#include "chrome/browser/ui/views/ime/ime_warning_bubble_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/location_bar/star_view.h"
#include "chrome/browser/ui/views/location_bar/zoom_bubble_view.h"
#include "chrome/browser/ui/views/new_back_shortcut_bubble.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/profiles/profile_indicator_icon.h"
#include "chrome/browser/ui/views/status_bubble_views.h"
#include "chrome/browser/ui/views/tabs/browser_tab_strip_controller.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/toolbar/app_menu_button.h"
#include "chrome/browser/ui/views/toolbar/browser_actions_container.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/translate/translate_bubble_view.h"
#include "chrome/browser/ui/views/update_recommended_message_box.h"
#include "chrome/browser/ui/window_sizer/window_sizer.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/command.h"
#include "chrome/common/features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/locale_settings.h"
#include "chrome/grit/theme_resources.h"
#include "components/app_modal/app_modal_dialog.h"
#include "components/app_modal/app_modal_dialog_queue.h"
#include "components/app_modal/native_app_modal_dialog.h"
#include "components/omnibox/browser/omnibox_popup_model.h"
#include "components/omnibox/browser/omnibox_popup_view.h"
#include "components/omnibox/browser/omnibox_view.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "components/translate/core/browser/language_state.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/content_accelerators/accelerator_util.h"
#include "ui/display/screen.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/native_theme/native_theme_dark_aura.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/external_focus_tracker.h"
#include "ui/views/focus/view_storage.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/widget/native_widget.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/ui/ash/ash_util.h"
#endif  // defined(OS_CHROMEOS)

#if !defined(OS_CHROMEOS)
#include "chrome/browser/ui/signin_view_controller.h"
#include "chrome/browser/ui/views/profiles/profile_chooser_view.h"
#endif  // !defined(OS_CHROMEOS)

#if defined(USE_AURA)
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#endif

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "chrome/browser/win/jumplist.h"
#include "chrome/browser/win/jumplist_factory.h"
#include "ui/gfx/color_palette.h"
#include "ui/native_theme/native_theme_win.h"
#include "ui/views/win/scoped_fullscreen_visibility.h"
#endif

#if BUILDFLAG(ENABLE_ONE_CLICK_SIGNIN)
#include "chrome/browser/ui/sync/one_click_signin_links_delegate_impl.h"
#include "chrome/browser/ui/views/sync/one_click_signin_dialog_view.h"
#endif

using base::TimeDelta;
using base::UserMetricsAction;
using content::NativeWebKeyboardEvent;
using content::WebContents;
using views::ColumnSet;
using views::GridLayout;
using web_modal::WebContentsModalDialogHost;

namespace {

// The name of a key to store on the window handle so that other code can
// locate this object using just the handle.
const char* const kBrowserViewKey = "__BROWSER_VIEW__";

// The number of milliseconds between loading animation frames.
const int kLoadingAnimationFrameTimeMs = 30;

// Paints the horizontal border separating the Bookmarks Bar from the Toolbar
// or page content according to |at_top| with |color|.
void PaintDetachedBookmarkBar(gfx::Canvas* canvas,
                              BookmarkBarView* view) {
  // Paint background for detached state; if animating, this is fade in/out.
  const ui::ThemeProvider* tp = view->GetThemeProvider();
  gfx::Rect fill_rect = view->GetLocalBounds();
  // We have to not color the top 1dp, because that should be painted by the
  // toolbar. We will, however, paint the 1px separator at the bottom of the
  // first dp. See crbug.com/610359
  fill_rect.Inset(0, 1, 0, 0);

  // In detached mode, the bar is meant to overlap with |contents_container_|.
  // The detached background color may be partially transparent, but the layer
  // for |view| must be painted opaquely to avoid subpixel anti-aliasing
  // artifacts, so we recreate the contents container base color here.
  canvas->FillRect(fill_rect,
                   tp->GetColor(ThemeProperties::COLOR_CONTROL_BACKGROUND));
  canvas->FillRect(
      fill_rect,
      tp->GetColor(ThemeProperties::COLOR_DETACHED_BOOKMARK_BAR_BACKGROUND));

  // Draw the separators above and below bookmark bar;
  // if animating, these are fading in/out.
  SkColor separator_color =
      tp->GetColor(ThemeProperties::COLOR_DETACHED_BOOKMARK_BAR_SEPARATOR);

  // For the bottom separator, increase the luminance. Either double it or halve
  // the distance to 1.0, whichever is less of a difference.
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(separator_color, &hsl);
  hsl.l = std::min((hsl.l + 1) / 2, hsl.l * 2);
  BrowserView::Paint1pxHorizontalLine(
      canvas, color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE),
      view->GetLocalBounds(), true);
}

// Paints the background (including the theme image behind content area) for
// the Bookmarks Bar when it is attached to the Toolbar into |bounds|.
// |background_origin| is the origin to use for painting the theme image.
void PaintBackgroundAttachedMode(gfx::Canvas* canvas,
                                 const ui::ThemeProvider* theme_provider,
                                 const gfx::Rect& bounds,
                                 const gfx::Point& background_origin) {
  canvas->DrawColor(theme_provider->GetColor(ThemeProperties::COLOR_TOOLBAR));

  // If there's a non-default background image, tile it.
  if (theme_provider->HasCustomImage(IDR_THEME_TOOLBAR)) {
    canvas->TileImageInt(*theme_provider->GetImageSkiaNamed(IDR_THEME_TOOLBAR),
                         background_origin.x(),
                         background_origin.y(),
                         bounds.x(),
                         bounds.y(),
                         bounds.width(),
                         bounds.height());
  }
}

void PaintAttachedBookmarkBar(gfx::Canvas* canvas,
                              BookmarkBarView* view,
                              BrowserView* browser_view,
                              int toolbar_overlap) {
  // Paint background for attached state.
  gfx::Point background_image_offset =
      browser_view->OffsetPointForToolbarBackgroundImage(
          gfx::Point(view->GetMirroredX(), view->y()));
  PaintBackgroundAttachedMode(canvas, view->GetThemeProvider(),
                              view->GetLocalBounds(), background_image_offset);
  if (view->height() >= toolbar_overlap) {
    BrowserView::Paint1pxHorizontalLine(
        canvas, view->GetThemeProvider()->GetColor(
                    ThemeProperties::COLOR_TOOLBAR_BOTTOM_SEPARATOR),
        view->GetLocalBounds(), true);
  }
}

bool GetGestureCommand(ui::GestureEvent* event, int* command) {
  DCHECK(command);
  *command = 0;
#if defined(OS_MACOSX)
  if (event->details().type() == ui::ET_GESTURE_SWIPE) {
    if (event->details().swipe_left()) {
      *command = IDC_BACK;
      return true;
    } else if (event->details().swipe_right()) {
      *command = IDC_FORWARD;
      return true;
    }
  }
#endif  // OS_MACOSX
  return false;
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// Delegate implementation for BrowserViewLayout. Usually just forwards calls
// into BrowserView.
class BrowserViewLayoutDelegateImpl : public BrowserViewLayoutDelegate {
 public:
  explicit BrowserViewLayoutDelegateImpl(BrowserView* browser_view)
      : browser_view_(browser_view) {}
  ~BrowserViewLayoutDelegateImpl() override {}

  // BrowserViewLayoutDelegate overrides:
  views::View* GetContentsWebView() const override {
    return browser_view_->contents_web_view_;
  }

  bool DownloadShelfNeedsLayout() const override {
    DownloadShelfView* download_shelf = browser_view_->download_shelf_.get();
    // Re-layout the shelf either if it is visible or if its close animation
    // is currently running.
    return download_shelf &&
           (download_shelf->IsShowing() || download_shelf->IsClosing());
  }

  bool IsTabStripVisible() const override {
    return browser_view_->IsTabStripVisible();
  }

  gfx::Rect GetBoundsForTabStripInBrowserView() const override {
    gfx::RectF bounds_f(browser_view_->frame()->GetBoundsForTabStrip(
        browser_view_->tabstrip()));
    views::View::ConvertRectToTarget(browser_view_->parent(), browser_view_,
        &bounds_f);
    return gfx::ToEnclosingRect(bounds_f);
  }

  int GetTopInsetInBrowserView(bool restored) const override {
    return browser_view_->frame()->GetTopInset(restored) -
        browser_view_->y();
  }

  int GetThemeBackgroundXInset() const override {
    // TODO(pkotwicz): Return the inset with respect to the left edge of the
    // BrowserView.
    return browser_view_->frame()->GetThemeBackgroundXInset();
  }

  bool IsToolbarVisible() const override {
    return browser_view_->IsToolbarVisible();
  }

  bool IsBookmarkBarVisible() const override {
    return browser_view_->IsBookmarkBarVisible();
  }

  ExclusiveAccessBubbleViews* GetExclusiveAccessBubble() const override {
    return browser_view_->exclusive_access_bubble();
  }

 private:
  BrowserView* browser_view_;

  DISALLOW_COPY_AND_ASSIGN(BrowserViewLayoutDelegateImpl);
};

// This class is used to paint the background for Bookmarks Bar.
class BookmarkBarViewBackground : public views::Background {
 public:
  BookmarkBarViewBackground(BrowserView* browser_view,
                            BookmarkBarView* bookmark_bar_view);

  // views:Background:
  void Paint(gfx::Canvas* canvas, views::View* view) const override;

 private:
  BrowserView* browser_view_;

  // The view hosting this background.
  BookmarkBarView* bookmark_bar_view_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkBarViewBackground);
};

BookmarkBarViewBackground::BookmarkBarViewBackground(
    BrowserView* browser_view,
    BookmarkBarView* bookmark_bar_view)
    : browser_view_(browser_view), bookmark_bar_view_(bookmark_bar_view) {}

void BookmarkBarViewBackground::Paint(gfx::Canvas* canvas,
                                      views::View* view) const {
  int toolbar_overlap = bookmark_bar_view_->GetToolbarOverlap();

  SkAlpha detached_alpha = static_cast<SkAlpha>(
      bookmark_bar_view_->size_animation().CurrentValueBetween(0xff, 0));
  if (detached_alpha != 0xff) {
    PaintAttachedBookmarkBar(canvas, bookmark_bar_view_, browser_view_,
                             toolbar_overlap);
  }

  if (!bookmark_bar_view_->IsDetached() || detached_alpha == 0)
    return;

  // While animating, set opacity to cross-fade between attached and detached
  // backgrounds including their respective separators.
  canvas->SaveLayerAlpha(detached_alpha);
  PaintDetachedBookmarkBar(canvas, bookmark_bar_view_);
  canvas->Restore();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, public:

// static
const char BrowserView::kViewClassName[] = "BrowserView";

BrowserView::BrowserView()
    : views::ClientView(nullptr, nullptr),
      last_focused_view_storage_id_(
          views::ViewStorage::GetInstance()->CreateStorageID()),
      frame_(nullptr),
      top_container_(nullptr),
      tabstrip_(nullptr),
      toolbar_(nullptr),
      find_bar_host_view_(nullptr),
      infobar_container_(nullptr),
      contents_web_view_(nullptr),
      devtools_web_view_(nullptr),
      contents_container_(nullptr),
      initialized_(false),
      handling_theme_changed_(false),
      in_process_fullscreen_(false),
      force_location_bar_focus_(false),
      activate_modal_dialog_factory_(this) {}

BrowserView::~BrowserView() {
  // All the tabs should have been destroyed already. If we were closed by the
  // OS with some tabs than the NativeBrowserFrame should have destroyed them.
  DCHECK_EQ(0, browser_->tab_strip_model()->count());

  // Stop the animation timer explicitly here to avoid running it in a nested
  // message loop, which may run by Browser destructor.
  loading_animation_timer_.Stop();

  // Immersive mode may need to reparent views before they are removed/deleted.
  immersive_mode_controller_.reset();

  browser_->tab_strip_model()->RemoveObserver(this);

  extensions::ExtensionCommandsGlobalRegistry* global_registry =
      extensions::ExtensionCommandsGlobalRegistry::Get(browser_->profile());
  if (global_registry->registry_for_active_window() ==
          extension_keybinding_registry_.get())
    global_registry->set_registry_for_active_window(nullptr);

  // We destroy the download shelf before |browser_| to remove its child
  // download views from the set of download observers (since the observed
  // downloads can be destroyed along with |browser_| and the observer
  // notifications will call back into deleted objects).
  BrowserViewLayout* browser_view_layout = GetBrowserViewLayout();
  if (browser_view_layout)
    browser_view_layout->set_download_shelf(nullptr);
  download_shelf_.reset();

  // The TabStrip attaches a listener to the model. Make sure we shut down the
  // TabStrip first so that it can cleanly remove the listener.
  if (tabstrip_) {
    tabstrip_->parent()->RemoveChildView(tabstrip_);
    if (browser_view_layout)
      browser_view_layout->set_tab_strip(nullptr);
    delete tabstrip_;
    tabstrip_ = nullptr;
  }
  // Child views maintain PrefMember attributes that point to
  // OffTheRecordProfile's PrefService which gets deleted by ~Browser.
  RemoveAllChildViews(true);
  toolbar_ = nullptr;

  // Explicitly set browser_ to null.
  browser_.reset();
}

void BrowserView::Init(Browser* browser) {
  browser_.reset(browser);
  browser_->tab_strip_model()->AddObserver(this);
  immersive_mode_controller_.reset(chrome::CreateImmersiveModeController());
}

// static
BrowserView* BrowserView::GetBrowserViewForNativeWindow(
    gfx::NativeWindow window) {
  views::Widget* widget = views::Widget::GetWidgetForNativeWindow(window);
  return widget ?
      reinterpret_cast<BrowserView*>(widget->GetNativeWindowProperty(
          kBrowserViewKey)) : nullptr;
}

// static
BrowserView* BrowserView::GetBrowserViewForBrowser(const Browser* browser) {
  return static_cast<BrowserView*>(browser->window());
}

// static
void BrowserView::Paint1pxHorizontalLine(gfx::Canvas* canvas,
                                         SkColor color,
                                         const gfx::Rect& bounds,
                                         bool at_bottom) {
  gfx::ScopedCanvas scoped_canvas(canvas);
  const float scale = canvas->UndoDeviceScaleFactor();
  gfx::RectF rect(gfx::ScaleRect(gfx::RectF(bounds), scale));
  const float inset = rect.height() - 1;
  rect.Inset(0, at_bottom ? inset : 0, 0, at_bottom ? 0 : inset);
  cc::PaintFlags flags;
  flags.setColor(color);
  canvas->sk_canvas()->drawRect(gfx::RectFToSkRect(rect), flags);
}

void BrowserView::InitStatusBubble() {
  status_bubble_.reset(
      new StatusBubbleViews(contents_web_view_, HasClientEdge()));
  contents_web_view_->SetStatusBubble(status_bubble_.get());
}

gfx::Rect BrowserView::GetToolbarBounds() const {
  gfx::Rect toolbar_bounds(toolbar_->bounds());
  if (toolbar_bounds.IsEmpty())
    return toolbar_bounds;
  // The apparent toolbar edges are outside the "real" toolbar edges.
  toolbar_bounds.Inset(-views::NonClientFrameView::kClientEdgeThickness, 0);
  return toolbar_bounds;
}

gfx::Rect BrowserView::GetFindBarBoundingBox() const {
  return GetBrowserViewLayout()->GetFindBarBoundingBox();
}

int BrowserView::GetTabStripHeight() const {
  // We want to return tabstrip_->height(), but we might be called in the midst
  // of layout, when that hasn't yet been updated to reflect the current state.
  // So return what the tabstrip height _ought_ to be right now.
  return IsTabStripVisible() ? tabstrip_->GetPreferredSize().height() : 0;
}

gfx::Point BrowserView::OffsetPointForToolbarBackgroundImage(
    const gfx::Point& point) const {
  // The background image starts tiling horizontally at the window left edge and
  // vertically at the top edge of the horizontal tab strip (or where it would
  // be).  We expect our parent's origin to be the window origin.
  gfx::Point window_point(point + GetMirroredPosition().OffsetFromOrigin());
  window_point.Offset(frame_->GetThemeBackgroundXInset(),
                      -frame_->GetTopInset(false));
  return window_point;
}

bool BrowserView::IsTabStripVisible() const {
  // Return false if this window does not normally display a tabstrip.
  if (!browser_->SupportsWindowFeature(Browser::FEATURE_TABSTRIP))
    return false;

  // Return false if the tabstrip has not yet been created (by InitViews()),
  // since callers may otherwise try to access it. Note that we can't just check
  // this alone, as the tabstrip is created unconditionally even for windows
  // that won't display it.
  return tabstrip_ != nullptr;
}

bool BrowserView::IsIncognito() const {
  return browser_->profile()->IsOffTheRecord();
}

bool BrowserView::IsGuestSession() const {
  return browser_->profile()->IsGuestSession();
}

bool BrowserView::IsRegularOrGuestSession() const {
  return profiles::IsRegularOrGuestSession(browser_.get());
}

bool BrowserView::HasClientEdge() const {
#if defined(OS_WIN)
  return base::win::GetVersion() < base::win::VERSION_WIN10 ||
         !frame_->ShouldUseNativeFrame();
#else
  return true;
#endif
}

bool BrowserView::GetAccelerator(int cmd_id,
                                 ui::Accelerator* accelerator) const {
  // We retrieve the accelerator information for standard accelerators
  // for cut, copy and paste.
  if (GetStandardAcceleratorForCommandId(cmd_id, accelerator))
    return true;
  // Else, we retrieve the accelerator information from the accelerator table.
  for (std::map<ui::Accelerator, int>::const_iterator it =
           accelerator_table_.begin(); it != accelerator_table_.end(); ++it) {
    if (it->second == cmd_id) {
      *accelerator = it->first;
      return true;
    }
  }
  // Else, we retrieve the accelerator information from Ash (if applicable).
  return GetAshAcceleratorForCommandId(cmd_id, accelerator);
}

bool BrowserView::IsAcceleratorRegistered(const ui::Accelerator& accelerator) {
  return accelerator_table_.find(accelerator) != accelerator_table_.end();
}

WebContents* BrowserView::GetActiveWebContents() const {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, BrowserWindow implementation:

void BrowserView::Show() {
#if !defined(OS_WIN) && !defined(OS_CHROMEOS)
  // The Browser associated with this browser window must become the active
  // browser at the time |Show()| is called. This is the natural behavior under
  // Windows and Chrome OS, but other platforms will not trigger
  // OnWidgetActivationChanged() until we return to the runloop. Therefore any
  // calls to Browser::GetLastActive() will return the wrong result if we do not
  // explicitly set it here.
  // A similar block also appears in BrowserWindowCocoa::Show().
  BrowserList::SetLastActive(browser());
#endif

  // If the window is already visible, just activate it.
  if (frame_->IsVisible()) {
    frame_->Activate();
    return;
  }

  // Showing the window doesn't make the browser window active right away.
  // This can cause SetFocusToLocationBar() to skip setting focus to the
  // location bar. To avoid this we explicilty let SetFocusToLocationBar()
  // know that it's ok to steal focus.
  force_location_bar_focus_ = true;

  // Setting the focus doesn't work when the window is invisible, so any focus
  // initialization that happened before this will be lost.
  //
  // We really "should" restore the focus whenever the window becomes unhidden,
  // but I think initializing is the only time where this can happen where
  // there is some focus change we need to pick up, and this is easier than
  // plumbing through an un-hide message all the way from the frame.
  //
  // If we do find there are cases where we need to restore the focus on show,
  // that should be added and this should be removed.
  RestoreFocus();

  frame_->Show();

  force_location_bar_focus_ = false;

  browser()->OnWindowDidShow();

  chrome::MaybeShowInvertBubbleView(this);
}

void BrowserView::ShowInactive() {
  if (!frame_->IsVisible())
    frame_->ShowInactive();
}

void BrowserView::Hide() {
  // Not implemented.
}

void BrowserView::SetBounds(const gfx::Rect& bounds) {
  ExitFullscreen();
  GetWidget()->SetBounds(bounds);
}

void BrowserView::Close() {
  frame_->Close();
}

void BrowserView::Activate() {
  frame_->Activate();
}

void BrowserView::Deactivate() {
  frame_->Deactivate();
}

bool BrowserView::IsActive() const {
  return frame_->IsActive();
}

void BrowserView::FlashFrame(bool flash) {
  frame_->FlashFrame(flash);
}

bool BrowserView::IsAlwaysOnTop() const {
  return false;
}

void BrowserView::SetAlwaysOnTop(bool always_on_top) {
  // Not implemented for browser windows.
  NOTIMPLEMENTED();
}

gfx::NativeWindow BrowserView::GetNativeWindow() const {
  // While the browser destruction is going on, the widget can already be gone,
  // but utility functions like FindBrowserWithWindow will still call this.
  return GetWidget() ? GetWidget()->GetNativeWindow() : nullptr;
}

StatusBubble* BrowserView::GetStatusBubble() {
  return status_bubble_.get();
}

namespace {
  // Only used by ToolbarSizeChanged() below, but placed here because template
  // arguments (to base::AutoReset<>) must have external linkage.
  enum CallState { NORMAL, REENTRANT, REENTRANT_FORCE_FAST_RESIZE };
}

void BrowserView::UpdateTitleBar() {
  // TODO(robliao): Remove ScopedTracker below once https://crbug.com/467185 is
  // fixed.
  tracked_objects::ScopedTracker tracking_profile1(
      FROM_HERE_WITH_EXPLICIT_FUNCTION("467185 BrowserView::UpdateTitleBar1"));
  frame_->UpdateWindowTitle();
  if (ShouldShowWindowIcon() && !loading_animation_timer_.IsRunning()) {
    // TODO(robliao): Remove ScopedTracker below once https://crbug.com/467185
    // is fixed.
    tracked_objects::ScopedTracker tracking_profile2(
        FROM_HERE_WITH_EXPLICIT_FUNCTION(
            "467185 BrowserView::UpdateTitleBar2"));
    frame_->UpdateWindowIcon();
  }
}

void BrowserView::BookmarkBarStateChanged(
    BookmarkBar::AnimateChangeType change_type) {
  if (bookmark_bar_view_.get()) {
    BookmarkBar::State new_state = browser_->bookmark_bar_state();

    // We don't properly support animating the bookmark bar to and from the
    // detached state in immersive fullscreen.
    bool detached_changed = (new_state == BookmarkBar::DETACHED) ||
        bookmark_bar_view_->IsDetached();
    if (detached_changed && immersive_mode_controller_->IsEnabled())
      change_type = BookmarkBar::DONT_ANIMATE_STATE_CHANGE;

    bookmark_bar_view_->SetBookmarkBarState(new_state, change_type);
  }
  if (MaybeShowBookmarkBar(GetActiveWebContents()))
    Layout();
}

void BrowserView::UpdateDevTools() {
  UpdateDevToolsForContents(GetActiveWebContents(), true);
  Layout();
}

void BrowserView::UpdateLoadingAnimations(bool should_animate) {
  if (should_animate) {
    if (!loading_animation_timer_.IsRunning()) {
      // Loads are happening, and the timer isn't running, so start it.
      loading_animation_timer_.Start(FROM_HERE,
          TimeDelta::FromMilliseconds(kLoadingAnimationFrameTimeMs), this,
          &BrowserView::LoadingAnimationCallback);
    }
  } else {
    if (loading_animation_timer_.IsRunning()) {
      loading_animation_timer_.Stop();
      // Loads are now complete, update the state if a task was scheduled.
      LoadingAnimationCallback();
    }
  }
}

void BrowserView::SetStarredState(bool is_starred) {
  GetLocationBarView()->SetStarToggled(is_starred);
}

void BrowserView::SetTranslateIconToggled(bool is_lit) {
  // Translate icon is never active on Views.
}

void BrowserView::OnActiveTabChanged(content::WebContents* old_contents,
                                     content::WebContents* new_contents,
                                     int index,
                                     int reason) {
  DCHECK(new_contents);

  // If |contents_container_| already has the correct WebContents, we can save
  // some work.  This also prevents extra events from being reported by the
  // Visibility API under Windows, as ChangeWebContents will briefly hide
  // the WebContents window.
  bool change_tab_contents =
      contents_web_view_->web_contents() != new_contents;

  // Update various elements that are interested in knowing the current
  // WebContents.

  // When we toggle the NTP floating bookmarks bar and/or the info bar,
  // we don't want any WebContents to be attached, so that we
  // avoid an unnecessary resize and re-layout of a WebContents.
  if (change_tab_contents) {
    contents_web_view_->SetWebContents(nullptr);
    devtools_web_view_->SetWebContents(nullptr);
  }

  // Do this before updating InfoBarContainer as the InfoBarContainer may
  // callback to us and trigger layout.
  if (bookmark_bar_view_.get()) {
    bookmark_bar_view_->SetBookmarkBarState(
        browser_->bookmark_bar_state(),
        BookmarkBar::DONT_ANIMATE_STATE_CHANGE);
  }

  infobar_container_->ChangeInfoBarManager(
      InfoBarService::FromWebContents(new_contents));

  if (old_contents && PermissionRequestManager::FromWebContents(old_contents))
    PermissionRequestManager::FromWebContents(old_contents)->HideBubble();

  if (new_contents && PermissionRequestManager::FromWebContents(new_contents)) {
    PermissionRequestManager::FromWebContents(new_contents)
        ->DisplayPendingRequests();
  }

  UpdateUIForContents(new_contents);

  // Layout for DevTools _before_ setting the both main and devtools WebContents
  // to avoid toggling the size of any of them.
  UpdateDevToolsForContents(new_contents, !change_tab_contents);

  if (change_tab_contents) {
    web_contents_close_handler_->ActiveTabChanged();
    contents_web_view_->SetWebContents(new_contents);
    // The second layout update should be no-op. It will just set the
    // DevTools WebContents.
    UpdateDevToolsForContents(new_contents, true);
  }

  if (!browser_->tab_strip_model()->closing_all() && GetWidget()->IsActive() &&
      GetWidget()->IsVisible()) {
    // We only restore focus if our window is visible, to avoid invoking blur
    // handlers when we are eventually shown.
    new_contents->RestoreFocus();
  }

  // Update all the UI bits.
  UpdateTitleBar();

  TranslateBubbleView::CloseCurrentBubble();
  ZoomBubbleView::CloseCurrentBubble();
}

void BrowserView::ZoomChangedForActiveTab(bool can_show_bubble) {
  bool app_menu_showing = toolbar_->app_menu_button() &&
                          toolbar_->app_menu_button()->IsMenuShowing();
  GetLocationBarView()->ZoomChangedForActiveTab(can_show_bubble &&
                                                !app_menu_showing);
}

gfx::Rect BrowserView::GetRestoredBounds() const {
  gfx::Rect bounds;
  ui::WindowShowState state;
  frame_->GetWindowPlacement(&bounds, &state);
  return bounds;
}

ui::WindowShowState BrowserView::GetRestoredState() const {
  gfx::Rect bounds;
  ui::WindowShowState state;
  frame_->GetWindowPlacement(&bounds, &state);
  return state;
}

gfx::Rect BrowserView::GetBounds() const {
  return frame_->GetWindowBoundsInScreen();
}

gfx::Size BrowserView::GetContentsSize() const {
  DCHECK(initialized_);
  return GetTabContentsContainerView()->size();
}

bool BrowserView::IsMaximized() const {
  return frame_->IsMaximized();
}

bool BrowserView::IsMinimized() const {
  return frame_->IsMinimized();
}

void BrowserView::Maximize() {
  frame_->Maximize();
}

void BrowserView::Minimize() {
  frame_->Minimize();
}

void BrowserView::Restore() {
  frame_->Restore();
}

void BrowserView::EnterFullscreen(const GURL& url,
                                  ExclusiveAccessBubbleType bubble_type) {
  if (IsFullscreen())
    return;  // Nothing to do.

  ProcessFullscreen(true, url, bubble_type);
}

void BrowserView::ExitFullscreen() {
  if (!IsFullscreen())
    return;  // Nothing to do.

  ProcessFullscreen(false, GURL(), EXCLUSIVE_ACCESS_BUBBLE_TYPE_NONE);
}

void BrowserView::UpdateExclusiveAccessExitBubbleContent(
    const GURL& url,
    ExclusiveAccessBubbleType bubble_type) {
  // Immersive mode has no exit bubble because it has a visible strip at the
  // top that gives the user a hover target.
  // TODO(jamescook): Figure out what to do with mouse-lock.
  if (bubble_type == EXCLUSIVE_ACCESS_BUBBLE_TYPE_NONE ||
      ShouldUseImmersiveFullscreenForUrl(url)) {
    exclusive_access_bubble_.reset();
    return;
  }

  if (exclusive_access_bubble_) {
    exclusive_access_bubble_->UpdateContent(url, bubble_type);
    return;
  }

  // Hide the backspace shortcut bubble, to avoid overlapping.
  new_back_shortcut_bubble_.reset();

  exclusive_access_bubble_.reset(
      new ExclusiveAccessBubbleViews(this, url, bubble_type));
}

void BrowserView::OnExclusiveAccessUserInput() {
  if (exclusive_access_bubble_.get())
    exclusive_access_bubble_->OnUserInput();
}

bool BrowserView::ShouldHideUIForFullscreen() const {
  // Immersive mode needs UI for the slide-down top panel.
  if (immersive_mode_controller_->IsEnabled())
    return false;

  return IsFullscreen();
}

bool BrowserView::IsFullscreen() const {
  return frame_->IsFullscreen();
}

bool BrowserView::IsFullscreenBubbleVisible() const {
  return exclusive_access_bubble_ != nullptr;
}

void BrowserView::MaybeShowNewBackShortcutBubble(bool forward) {
  if (!new_back_shortcut_bubble_ || !new_back_shortcut_bubble_->IsVisible()) {
    // Show the bubble at most five times.
    PrefService* prefs = browser_->profile()->GetPrefs();
    int shown_count = prefs->GetInteger(prefs::kBackShortcutBubbleShownCount);
    constexpr int kMaxShownCount = 5;
    if (shown_count >= kMaxShownCount)
      return;

    // Only show the bubble when the user presses a shortcut twice within three
    // seconds.
    const base::TimeTicks now = base::TimeTicks::Now();
    constexpr base::TimeDelta kRepeatWindow = base::TimeDelta::FromSeconds(3);
    if (last_back_shortcut_press_time_.is_null() ||
        ((now - last_back_shortcut_press_time_) > kRepeatWindow)) {
      last_back_shortcut_press_time_ = now;
      return;
    }

    // Hide the exclusive access bubble, to avoid overlapping.
    exclusive_access_bubble_.reset();

    new_back_shortcut_bubble_.reset(new NewBackShortcutBubble(this));
    prefs->SetInteger(prefs::kBackShortcutBubbleShownCount, shown_count + 1);
    last_back_shortcut_press_time_ = base::TimeTicks();
  }

  new_back_shortcut_bubble_->UpdateContent(forward);
}

void BrowserView::HideNewBackShortcutBubble() {
  if (new_back_shortcut_bubble_)
    new_back_shortcut_bubble_->Hide();
}

void BrowserView::RestoreFocus() {
  WebContents* selected_web_contents = GetActiveWebContents();
  if (selected_web_contents)
    selected_web_contents->RestoreFocus();
}

void BrowserView::FullscreenStateChanged() {
  CHECK(!IsFullscreen());
  ProcessFullscreen(false, GURL(), EXCLUSIVE_ACCESS_BUBBLE_TYPE_NONE);
}

LocationBar* BrowserView::GetLocationBar() const {
  return GetLocationBarView();
}

void BrowserView::SetFocusToLocationBar(bool select_all) {
  // On Windows, changing focus to the location bar causes the browser window to
  // become active. This can steal focus if the user has another window open
  // already. On Chrome OS, changing focus makes a view believe it has a focus
  // even if the widget doens't have a focus. Either cases, we need to ignore
  // this when the browser window isn't active.
#if defined(OS_WIN) || defined(OS_CHROMEOS)
  if (!force_location_bar_focus_ && !IsActive())
    return;
#endif

  // Temporarily reveal the top-of-window views (if not already revealed) so
  // that the location bar view is visible and is considered focusable. If the
  // location bar view gains focus, |immersive_mode_controller_| will keep the
  // top-of-window views revealed.
  std::unique_ptr<ImmersiveRevealedLock> focus_reveal_lock(
      immersive_mode_controller_->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_YES));

  LocationBarView* location_bar = GetLocationBarView();
  if (location_bar->omnibox_view()->IsFocusable()) {
    // Location bar got focus.
    location_bar->FocusLocation(select_all);
  } else {
    // If none of location bar got focus, then clear focus.
    views::FocusManager* focus_manager = GetFocusManager();
    DCHECK(focus_manager);
    focus_manager->ClearFocus();
  }
}

void BrowserView::UpdateReloadStopState(bool is_loading, bool force) {
  if (toolbar_->reload_button()) {
    toolbar_->reload_button()->ChangeMode(
        is_loading ? ReloadButton::MODE_STOP : ReloadButton::MODE_RELOAD,
        force);
  }
}

void BrowserView::UpdateToolbar(content::WebContents* contents) {
  // We may end up here during destruction.
  if (toolbar_)
    toolbar_->Update(contents);
}

void BrowserView::ResetToolbarTabState(content::WebContents* contents) {
  // We may end up here during destruction.
  if (toolbar_)
    toolbar_->ResetTabState(contents);
}

void BrowserView::FocusToolbar() {
  // Temporarily reveal the top-of-window views (if not already revealed) so
  // that the toolbar is visible and is considered focusable. If the
  // toolbar gains focus, |immersive_mode_controller_| will keep the
  // top-of-window views revealed.
  std::unique_ptr<ImmersiveRevealedLock> focus_reveal_lock(
      immersive_mode_controller_->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_YES));

  // Start the traversal within the main toolbar. SetPaneFocus stores
  // the current focused view before changing focus.
  toolbar_->SetPaneFocus(nullptr);
}

ToolbarActionsBar* BrowserView::GetToolbarActionsBar() {
  return toolbar_ && toolbar_->browser_actions() ?
      toolbar_->browser_actions()->toolbar_actions_bar() : nullptr;
}

void BrowserView::ToolbarSizeChanged(bool is_animating) {
  // The call to SetMaxTopArrowHeight() below can result in reentrancy;
  // |call_state| tracks whether we're reentrant.  We can't just early-return in
  // this case because we need to layout again so the infobar container's bounds
  // are set correctly.
  static CallState call_state = NORMAL;

  // A reentrant call can (and should) use the fast resize path unless both it
  // and the normal call are both non-animating.
  bool use_fast_resize =
      is_animating || (call_state == REENTRANT_FORCE_FAST_RESIZE);
  if (use_fast_resize)
    contents_web_view_->SetFastResize(true);
  UpdateUIForContents(GetActiveWebContents());
  if (use_fast_resize)
    contents_web_view_->SetFastResize(false);

  // Inform the InfoBarContainer that the distance to the location icon may have
  // changed.  We have to do this after the block above so that the toolbars are
  // laid out correctly for calculating the maximum arrow height below.
  {
    base::AutoReset<CallState> resetter(&call_state,
        is_animating ? REENTRANT_FORCE_FAST_RESIZE : REENTRANT);
    SetMaxTopArrowHeight(GetMaxTopInfoBarArrowHeight(), infobar_container_);
  }

  // When transitioning from animating to not animating we need to make sure the
  // contents_container_ gets layed out. If we don't do this and the bounds
  // haven't changed contents_container_ won't get a Layout out and we'll end up
  // with a gray rect because the clip wasn't updated.  Note that a reentrant
  // call never needs to do this, because after it returns, the normal call
  // wrapping it will do it.
  if ((call_state == NORMAL) && !is_animating) {
    contents_web_view_->InvalidateLayout();
    contents_container_->Layout();
  }
}

void BrowserView::FocusBookmarksToolbar() {
  DCHECK(!immersive_mode_controller_->IsEnabled());
  if (bookmark_bar_view_.get() &&
      bookmark_bar_view_->visible() &&
      bookmark_bar_view_->GetPreferredSize().height() != 0) {
    bookmark_bar_view_->SetPaneFocusAndFocusDefault();
  }
}

void BrowserView::FocusInfobars() {
  if (infobar_container_->child_count() > 0)
    infobar_container_->SetPaneFocusAndFocusDefault();
}

void BrowserView::FocusAppMenu() {
  // Chrome doesn't have a traditional menu bar, but it has a menu button in the
  // main toolbar that plays the same role.  If the user presses a key that
  // would typically focus the menu bar, tell the toolbar to focus the menu
  // button.  If the user presses the key again, return focus to the previous
  // location.
  //
  // Not used on the Mac, which has a normal menu bar.
  if (toolbar_->IsAppMenuFocused()) {
    RestoreFocus();
  } else {
    DCHECK(!immersive_mode_controller_->IsEnabled());
    toolbar_->SetPaneFocusAndFocusAppMenu();
  }
}

void BrowserView::RotatePaneFocus(bool forwards) {
  GetFocusManager()->RotatePaneFocus(
      forwards ?
          views::FocusManager::kForward : views::FocusManager::kBackward,
      views::FocusManager::kWrap);
}

void BrowserView::DestroyBrowser() {
  // After this returns other parts of Chrome are going to be shutdown. Close
  // the window now so that we are deleted immediately and aren't left holding
  // references to deleted objects.
  GetWidget()->RemoveObserver(this);
  GetLocationBar()->GetOmniboxView()->model()->popup_model()->RemoveObserver(
      this);
  frame_->CloseNow();
}

bool BrowserView::IsBookmarkBarVisible() const {
  if (!browser_->SupportsWindowFeature(Browser::FEATURE_BOOKMARKBAR))
    return false;
  if (!bookmark_bar_view_.get())
    return false;
  if (bookmark_bar_view_->GetPreferredSize().height() == 0)
    return false;
  // New tab page needs visible bookmarks even when top-views are hidden.
  if (immersive_mode_controller_->ShouldHideTopViews() &&
      !bookmark_bar_view_->IsDetached())
    return false;
  return true;
}

bool BrowserView::IsBookmarkBarAnimating() const {
  return bookmark_bar_view_.get() &&
         bookmark_bar_view_->size_animation().is_animating();
}

bool BrowserView::IsTabStripEditable() const {
  return tabstrip_->IsTabStripEditable();
}

bool BrowserView::IsToolbarVisible() const {
  if (immersive_mode_controller_->ShouldHideTopViews())
    return false;
  // It's possible to reach here before we've been notified of being added to a
  // widget, so |toolbar_| is still null.  Return false in this case so callers
  // don't assume they can access the toolbar yet.
  return (browser_->SupportsWindowFeature(Browser::FEATURE_TOOLBAR) ||
          browser_->SupportsWindowFeature(Browser::FEATURE_LOCATIONBAR)) &&
         toolbar_;
}

bool BrowserView::IsToolbarShowing() const {
  return IsToolbarVisible();
}

void BrowserView::ShowUpdateChromeDialog() {
  UpdateRecommendedMessageBox::Show(GetNativeWindow());
}

void BrowserView::ShowBookmarkBubble(const GURL& url, bool already_bookmarked) {
  toolbar_->ShowBookmarkBubble(url, already_bookmarked,
                               bookmark_bar_view_.get());
}

void BrowserView::ShowBookmarkAppBubble(
    const WebApplicationInfo& web_app_info,
    const ShowBookmarkAppBubbleCallback& callback) {
  BookmarkAppConfirmationView::CreateAndShow(GetNativeWindow(), web_app_info,
                                             callback);
}

autofill::SaveCardBubbleView* BrowserView::ShowSaveCreditCardBubble(
    content::WebContents* web_contents,
    autofill::SaveCardBubbleController* controller,
    bool is_user_gesture) {
  return toolbar_->ShowSaveCreditCardBubble(web_contents, controller,
                                            is_user_gesture);
}

ShowTranslateBubbleResult BrowserView::ShowTranslateBubble(
    content::WebContents* web_contents,
    translate::TranslateStep step,
    translate::TranslateErrors::Type error_type,
    bool is_user_gesture) {
  if (contents_web_view_->HasFocus() &&
      !GetLocationBarView()->IsMouseHovered() &&
      web_contents->IsFocusedElementEditable()) {
    return ShowTranslateBubbleResult::EDITABLE_FIELD_IS_ACTIVE;
  }

  translate::LanguageState& language_state =
      ChromeTranslateClient::FromWebContents(web_contents)->GetLanguageState();
  language_state.SetTranslateEnabled(true);

  if (IsMinimized())
    return ShowTranslateBubbleResult::BROWSER_WINDOW_MINIMIZED;

  toolbar_->ShowTranslateBubble(web_contents, step, error_type,
                                is_user_gesture);
  return ShowTranslateBubbleResult::SUCCESS;
}

#if BUILDFLAG(ENABLE_ONE_CLICK_SIGNIN)
void BrowserView::ShowOneClickSigninConfirmation(
    const base::string16& email,
    const StartSyncCallback& start_sync_callback) {
  std::unique_ptr<OneClickSigninLinksDelegate> delegate(
      new OneClickSigninLinksDelegateImpl(browser()));
  OneClickSigninDialogView::ShowDialog(email, std::move(delegate),
                                       GetNativeWindow(), start_sync_callback);
}
#endif

void BrowserView::SetDownloadShelfVisible(bool visible) {
  DCHECK(download_shelf_);
  browser_->UpdateDownloadShelfVisibility(visible);

  // SetDownloadShelfVisible can force-close the shelf, so make sure we lay out
  // everything correctly, as if the animation had finished. This doesn't
  // matter for showing the shelf, as the show animation will do it.
  ToolbarSizeChanged(false);
}

bool BrowserView::IsDownloadShelfVisible() const {
  return download_shelf_.get() && download_shelf_->IsShowing();
}

DownloadShelf* BrowserView::GetDownloadShelf() {
  DCHECK(browser_->SupportsWindowFeature(Browser::FEATURE_DOWNLOADSHELF));
  if (!download_shelf_.get()) {
    download_shelf_.reset(new DownloadShelfView(browser_.get(), this));
    download_shelf_->set_owned_by_client();
    GetBrowserViewLayout()->set_download_shelf(download_shelf_.get());
  }
  return download_shelf_.get();
}

void BrowserView::ConfirmBrowserCloseWithPendingDownloads(
    int download_count,
    Browser::DownloadClosePreventionType dialog_type,
    bool app_modal,
    const base::Callback<void(bool)>& callback) {
  DownloadInProgressDialogView::Show(
      GetNativeWindow(), download_count, dialog_type, app_modal, callback);
}

void BrowserView::UserChangedTheme() {
  frame_->FrameTypeChanged();
}

void BrowserView::ShowPageInfo(
    Profile* profile,
    content::WebContents* web_contents,
    const GURL& virtual_url,
    const security_state::SecurityInfo& security_info) {
  PageInfoBubbleView::ShowBubble(
      GetLocationBarView()->GetSecurityBubbleAnchorView(),
      GetLocationBarView()->location_icon_view(), gfx::Rect(), profile,
      web_contents, virtual_url, security_info);
}

void BrowserView::ShowAppMenu() {
  if (!toolbar_->app_menu_button())
    return;

  // Keep the top-of-window views revealed as long as the app menu is visible.
  std::unique_ptr<ImmersiveRevealedLock> revealed_lock(
      immersive_mode_controller_->GetRevealedLock(
          ImmersiveModeController::ANIMATE_REVEAL_NO));

  toolbar_->app_menu_button()->Activate(nullptr);
}

content::KeyboardEventProcessingResult BrowserView::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
  if ((event.GetType() != blink::WebInputEvent::kRawKeyDown) &&
      (event.GetType() != blink::WebInputEvent::kKeyUp)) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  views::FocusManager* focus_manager = GetFocusManager();
  DCHECK(focus_manager);

  if (focus_manager->shortcut_handling_suspended())
    return content::KeyboardEventProcessingResult::NOT_HANDLED;

  ui::Accelerator accelerator =
      ui::GetAcceleratorFromNativeWebKeyboardEvent(event);

  // What we have to do here is as follows:
  // - If the |browser_| is for an app, do nothing.
  // - On CrOS if |accelerator| is deprecated, we allow web contents to consume
  //   it if needed.
  // - If the |browser_| is not for an app, and the |accelerator| is not
  //   associated with the browser (e.g. an Ash shortcut), process it.
  // - If the |browser_| is not for an app, and the |accelerator| is associated
  //   with the browser, and it is a reserved one (e.g. Ctrl+w), process it.
  // - If the |browser_| is not for an app, and the |accelerator| is associated
  //   with the browser, and it is not a reserved one, do nothing.

  if (browser_->is_app()) {
    // Let all keys fall through to a v1 app's web content, even accelerators.
    // We don't use NOT_HANDLED_IS_SHORTCUT here. If we do that, the app
    // might not be able to see a subsequent Char event. See OnHandleInputEvent
    // in content/renderer/render_widget.cc for details.
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

#if defined(OS_CHROMEOS)
  if (ash_util::IsAcceleratorDeprecated(accelerator)) {
    return (event.GetType() == blink::WebInputEvent::kRawKeyDown)
               ? content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
#endif  // defined(OS_CHROMEOS)

  if (frame_->PreHandleKeyboardEvent(event))
    return content::KeyboardEventProcessingResult::HANDLED;

#if defined(OS_CHROMEOS)
  if (event.os_event && event.os_event->IsKeyEvent() &&
      ash_util::WillAshProcessAcceleratorForEvent(
          *event.os_event->AsKeyEvent())) {
    return content::KeyboardEventProcessingResult::HANDLED_DONT_UPDATE_EVENT;
  }
#endif

  chrome::BrowserCommandController* controller = browser_->command_controller();

  // Here we need to retrieve the command id (if any) associated to the
  // keyboard event. Instead of looking up the command id in the
  // |accelerator_table_| by ourselves, we block the command execution of
  // the |browser_| object then send the keyboard event to the
  // |focus_manager| as if we are activating an accelerator key.
  // Then we can retrieve the command id from the |browser_| object.
  bool original_block_command_state = controller->block_command_execution();
  controller->SetBlockCommandExecution(true);
  // If the |accelerator| is a non-browser shortcut (e.g. Ash shortcut), the
  // command execution cannot be blocked and true is returned. However, it is
  // okay as long as is_app() is false. See comments in this function.
  const bool processed = focus_manager->ProcessAccelerator(accelerator);
  const int id = controller->GetLastBlockedCommand(nullptr);
  controller->SetBlockCommandExecution(original_block_command_state);

  // Executing the command may cause |this| object to be destroyed.
  if (controller->IsReservedCommandOrKey(id, event)) {
    UpdateAcceleratorMetrics(accelerator, id);
    return chrome::ExecuteCommand(browser_.get(), id)
               ? content::KeyboardEventProcessingResult::HANDLED
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  if (id != -1) {
    // |accelerator| is a non-reserved browser shortcut (e.g. Ctrl+f).
    return (event.GetType() == blink::WebInputEvent::kRawKeyDown)
               ? content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT
               : content::KeyboardEventProcessingResult::NOT_HANDLED;
  }

  if (processed) {
    // |accelerator| is a non-browser shortcut (e.g. F4-F10 on Ash). Report
    // that we handled it.
    return content::KeyboardEventProcessingResult::HANDLED;
  }

  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

void BrowserView::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  if (frame_->HandleKeyboardEvent(event))
    return;

  unhandled_keyboard_event_handler_.HandleKeyboardEvent(event,
                                                        GetFocusManager());
}

// TODO(devint): http://b/issue?id=1117225 Cut, Copy, and Paste are always
// enabled in the page menu regardless of whether the command will do
// anything. When someone selects the menu item, we just act as if they hit
// the keyboard shortcut for the command by sending the associated key press
// to windows. The real fix to this bug is to disable the commands when they
// won't do anything. We'll need something like an overall clipboard command
// manager to do that.
void BrowserView::CutCopyPaste(int command_id) {
  // If a WebContents is focused, call its member method.
  //
  // We could make WebContents register accelerators and then just use the
  // plumbing for accelerators below to dispatch these, but it's not clear
  // whether that would still allow keypresses of ctrl-X/C/V to be sent as
  // key events (and not accelerators) to the WebContents so it can give the web
  // page a chance to override them.
  WebContents* contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (contents) {
    void (WebContents::*method)();
    if (command_id == IDC_CUT)
      method = &content::WebContents::Cut;
    else if (command_id == IDC_COPY)
      method = &content::WebContents::Copy;
    else
      method = &content::WebContents::Paste;
    if (DoCutCopyPasteForWebContents(contents, method))
      return;

    WebContents* devtools =
        DevToolsWindow::GetInTabWebContents(contents, nullptr);
    if (devtools && DoCutCopyPasteForWebContents(devtools, method))
      return;
  }

  // Any Views which want to handle the clipboard commands in the Chrome menu
  // should:
  //   (a) Register ctrl-X/C/V as accelerators
  //   (b) Implement CanHandleAccelerators() to not return true unless they're
  //       focused, as the FocusManager will try all registered accelerator
  //       handlers, not just the focused one.
  // Currently, Textfield (which covers the omnibox and find bar, and likely any
  // other native UI in the future that wants to deal with clipboard commands)
  // does the above.
  ui::Accelerator accelerator;
  GetAccelerator(command_id, &accelerator);
  GetFocusManager()->ProcessAccelerator(accelerator);
}

WindowOpenDisposition BrowserView::GetDispositionForPopupBounds(
    const gfx::Rect& bounds) {
  return WindowOpenDisposition::NEW_POPUP;
}

FindBar* BrowserView::CreateFindBar() {
  return new FindBarHost(this);
}

WebContentsModalDialogHost* BrowserView::GetWebContentsModalDialogHost() {
  return GetBrowserViewLayout()->GetWebContentsModalDialogHost();
}

BookmarkBarView* BrowserView::GetBookmarkBarView() const {
  return bookmark_bar_view_.get();
}

LocationBarView* BrowserView::GetLocationBarView() const {
  return toolbar_ ? toolbar_->location_bar() : nullptr;
}

views::View* BrowserView::GetTabContentsContainerView() const {
  return contents_web_view_;
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, TabStripModelObserver implementation:

void BrowserView::TabInsertedAt(TabStripModel* tab_strip_model,
                                WebContents* contents,
                                int index,
                                bool foreground) {
#if defined(USE_AURA)
  // WebContents inserted in tabs might not have been added to the root
  // window yet. Per http://crbug/342672 add them now since drawing the
  // WebContents requires root window specific data - information about
  // the screen the WebContents is drawn on, for example.
  if (!contents->GetNativeView()->GetRootWindow()) {
    aura::Window* window = contents->GetNativeView();
    aura::Window* root_window = GetNativeWindow()->GetRootWindow();
    aura::client::ParentWindowWithContext(
        window, root_window, root_window->GetBoundsInScreen());
    DCHECK(contents->GetNativeView()->GetRootWindow());
  }
#endif
  web_contents_close_handler_->TabInserted();
}

void BrowserView::TabDetachedAt(WebContents* contents, int index) {
  if (PermissionRequestManager::FromWebContents(contents))
    PermissionRequestManager::FromWebContents(contents)->HideBubble();

  // We use index here rather than comparing |contents| because by this time
  // the model has already removed |contents| from its list, so
  // browser_->GetActiveWebContents() will return null or something else.
  if (index == browser_->tab_strip_model()->active_index()) {
    // We need to reset the current tab contents to null before it gets
    // freed. This is because the focus manager performs some operations
    // on the selected WebContents when it is removed.
    web_contents_close_handler_->ActiveTabChanged();
    contents_web_view_->SetWebContents(nullptr);
    infobar_container_->ChangeInfoBarManager(nullptr);
    UpdateDevToolsForContents(nullptr, true);
  }
}

void BrowserView::TabDeactivated(WebContents* contents) {
  if (PermissionRequestManager::FromWebContents(contents))
    PermissionRequestManager::FromWebContents(contents)->HideBubble();

  // We do not store the focus when closing the tab to work-around bug 4633.
  // Some reports seem to show that the focus manager and/or focused view can
  // be garbage at that point, it is not clear why.
  if (!contents->IsBeingDestroyed())
    contents->StoreFocus();
}

void BrowserView::TabStripEmpty() {
  // Make sure all optional UI is removed before we are destroyed, otherwise
  // there will be consequences (since our view hierarchy will still have
  // references to freed views).
  UpdateUIForContents(nullptr);
}

void BrowserView::WillCloseAllTabs() {
  web_contents_close_handler_->WillCloseAllTabs();
}

void BrowserView::CloseAllTabsCanceled() {
  web_contents_close_handler_->CloseAllTabsCanceled();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ui::AcceleratorProvider implementation:

bool BrowserView::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  // Let's let the ToolbarView own the canonical implementation of this method.
  return toolbar_->GetAcceleratorForCommandId(command_id, accelerator);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, views::WidgetDelegate implementation:

bool BrowserView::CanResize() const {
  return true;
}

bool BrowserView::CanMaximize() const {
  return true;
}

bool BrowserView::CanMinimize() const {
  return true;
}

bool BrowserView::CanActivate() const {
  app_modal::AppModalDialogQueue* queue =
      app_modal::AppModalDialogQueue::GetInstance();
  if (!queue->active_dialog() || !queue->active_dialog()->native_dialog() ||
      !queue->active_dialog()->native_dialog()->IsShowing()) {
    return true;
  }

#if defined(USE_AURA) && defined(OS_CHROMEOS)
  // On Aura window manager controls all windows so settings focus via PostTask
  // will make only worse because posted task will keep trying to steal focus.
  queue->ActivateModalDialog();
#else
  // If another browser is app modal, flash and activate the modal browser. This
  // has to be done in a post task, otherwise if the user clicked on a window
  // that doesn't have the modal dialog the windows keep trying to get the focus
  // from each other on Windows. http://crbug.com/141650.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&BrowserView::ActivateAppModalDialog,
                                activate_modal_dialog_factory_.GetWeakPtr()));
#endif
  return false;
}

base::string16 BrowserView::GetWindowTitle() const {
  return browser_->GetWindowTitleForCurrentTab(true /* include_app_name */);
}

base::string16 BrowserView::GetAccessibleWindowTitle() const {
  const bool include_app_name = false;
  int active_index = browser_->tab_strip_model()->active_index();
  if (active_index > -1) {
    if (IsIncognito()) {
      return l10n_util::GetStringFUTF16(
          IDS_ACCESSIBLE_INCOGNITO_WINDOW_TITLE_FORMAT,
          GetAccessibleTabLabel(include_app_name, active_index));
    }
    return GetAccessibleTabLabel(include_app_name, active_index);
  }
  if (IsIncognito()) {
    return l10n_util::GetStringFUTF16(
        IDS_ACCESSIBLE_INCOGNITO_WINDOW_TITLE_FORMAT,
        browser_->GetWindowTitleForCurrentTab(include_app_name));
  }
  return browser_->GetWindowTitleForCurrentTab(include_app_name);
}

base::string16 BrowserView::GetAccessibleTabLabel(bool include_app_name,
                                                  int index) const {
  // ChromeVox provides an invalid index on browser start up before
  // any tabs are created.
  if (index == -1)
    return base::string16();

  base::string16 window_title =
      browser_->GetWindowTitleForTab(include_app_name, index);
  const TabRendererData& data = tabstrip_->tab_at(index)->data();

  // Tab has crashed.
  if (data.IsCrashed()) {
    return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_CRASHED_FORMAT,
                                      window_title);
  }
  // Network error interstitial.
  if (data.network_state == TabRendererData::NETWORK_STATE_ERROR) {
    return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_NETWORK_ERROR_FORMAT,
                                      window_title);
  }
  // Alert tab states.
  switch (data.alert_state) {
    case TabAlertState::AUDIO_PLAYING:
      return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_AUDIO_PLAYING_FORMAT,
                                        window_title);
    case TabAlertState::USB_CONNECTED:
      return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_USB_CONNECTED_FORMAT,
                                        window_title);
    case TabAlertState::BLUETOOTH_CONNECTED:
      return l10n_util::GetStringFUTF16(
          IDS_TAB_AX_LABEL_BLUETOOTH_CONNECTED_FORMAT, window_title);
    case TabAlertState::MEDIA_RECORDING:
      return l10n_util::GetStringFUTF16(
          IDS_TAB_AX_LABEL_MEDIA_RECORDING_FORMAT, window_title);
    case TabAlertState::AUDIO_MUTING:
      return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_AUDIO_MUTING_FORMAT,
                                        window_title);
    case TabAlertState::TAB_CAPTURING:
      return l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_TAB_CAPTURING_FORMAT,
                                        window_title);
    case TabAlertState::NONE:
      return window_title;
  }
  return base::string16();
}

void BrowserView::NativeThemeUpdated(const ui::NativeTheme* theme) {
  // We don't handle theme updates in OnThemeChanged() as that function is
  // called while views are being iterated over. Please see
  // View::PropagateNativeThemeChanged() for details. The theme update
  // handling in UserChangedTheme() can cause views to be nuked or created
  // which is a bad thing during iteration.

  // Do not handle native theme changes before the browser view is initialized.
  if (!initialized_)
    return;
  // Don't infinitely recurse.
  if (!handling_theme_changed_)
    UserChangedTheme();
  chrome::MaybeShowInvertBubbleView(this);
}

views::View* BrowserView::GetInitiallyFocusedView() {
  return nullptr;
}

bool BrowserView::ShouldShowWindowTitle() const {
#if defined(OS_CHROMEOS)
  // For Chrome OS only, trusted windows (apps and settings) do not show a
  // title, crbug.com/119411. Child windows (i.e. popups) do show a title.
  if (browser_->is_trusted_source())
    return false;
#endif  // OS_CHROMEOS

  return browser_->SupportsWindowFeature(Browser::FEATURE_TITLEBAR);
}

gfx::ImageSkia BrowserView::GetWindowAppIcon() {
  if (browser_->is_app()) {
    WebContents* contents = browser_->tab_strip_model()->GetActiveWebContents();
    extensions::TabHelper* extensions_tab_helper =
        contents ? extensions::TabHelper::FromWebContents(contents) : nullptr;
    if (extensions_tab_helper && extensions_tab_helper->GetExtensionAppIcon())
      return gfx::ImageSkia::CreateFrom1xBitmap(
          *extensions_tab_helper->GetExtensionAppIcon());
  }

  return GetWindowIcon();
}

gfx::ImageSkia BrowserView::GetWindowIcon() {
  // Use the default icon for devtools.
  if (browser_->is_devtools())
    return gfx::ImageSkia();

  if (browser_->is_app() || browser_->is_type_popup())
    return browser_->GetCurrentPageIcon().AsImageSkia();
  return gfx::ImageSkia();
}

bool BrowserView::ShouldShowWindowIcon() const {
#if defined(OS_CHROMEOS)
  // For Chrome OS only, trusted windows (apps and settings) do not show an
  // icon, crbug.com/119411. Child windows (i.e. popups) do show an icon.
  if (browser_->is_trusted_source())
    return false;
#endif  // OS_CHROMEOS

  return browser_->SupportsWindowFeature(Browser::FEATURE_TITLEBAR);
}

bool BrowserView::ExecuteWindowsCommand(int command_id) {
  // This function handles WM_SYSCOMMAND, WM_APPCOMMAND, and WM_COMMAND.
#if defined(OS_WIN)
  if (command_id == IDC_DEBUG_FRAME_TOGGLE)
    GetWidget()->DebugToggleFrameType();
#endif
  // Translate WM_APPCOMMAND command ids into a command id that the browser
  // knows how to handle.
  int command_id_from_app_command = GetCommandIDForAppCommandID(command_id);
  if (command_id_from_app_command != -1)
    command_id = command_id_from_app_command;

  return chrome::ExecuteCommand(browser_.get(), command_id);
}

std::string BrowserView::GetWindowName() const {
  return chrome::GetWindowName(browser_.get());
}

void BrowserView::SaveWindowPlacement(const gfx::Rect& bounds,
                                      ui::WindowShowState show_state) {
  // If IsFullscreen() is true, we've just changed into fullscreen mode, and
  // we're catching the going-into-fullscreen sizing and positioning calls,
  // which we want to ignore.
  if (!IsFullscreen() && frame_->ShouldSaveWindowPlacement() &&
      chrome::ShouldSaveWindowPlacement(browser_.get())) {
    WidgetDelegate::SaveWindowPlacement(bounds, show_state);
    chrome::SaveWindowPlacement(browser_.get(), bounds, show_state);
  }
}

bool BrowserView::GetSavedWindowPlacement(
    const views::Widget* widget,
    gfx::Rect* bounds,
    ui::WindowShowState* show_state) const {
  chrome::GetSavedWindowBoundsAndShowState(browser_.get(), bounds, show_state);

  if (chrome::SavedBoundsAreContentBounds(browser_.get())) {
    // This is normal non-app popup window. The value passed in |bounds|
    // represents two pieces of information:
    // - the position of the window, in screen coordinates (outer position).
    // - the size of the content area (inner size).
    // We need to use these values to determine the appropriate size and
    // position of the resulting window.
    if (IsToolbarVisible()) {
      // If we're showing the toolbar, we need to adjust |*bounds| to include
      // its desired height, since the toolbar is considered part of the
      // window's client area as far as GetWindowBoundsForClientBounds is
      // concerned...
      bounds->set_height(
          bounds->height() + toolbar_->GetPreferredSize().height());
    }

    gfx::Rect window_rect = frame_->non_client_view()->
        GetWindowBoundsForClientBounds(*bounds);
    window_rect.set_origin(bounds->origin());

    // When we are given x/y coordinates of 0 on a created popup window,
    // assume none were given by the window.open() command.
    if (window_rect.x() == 0 && window_rect.y() == 0) {
      gfx::Size size = window_rect.size();
      window_rect.set_origin(WindowSizer::GetDefaultPopupOrigin(size));
    }

    *bounds = window_rect;
    *show_state = ui::SHOW_STATE_NORMAL;
  }

  // We return true because we can _always_ locate reasonable bounds using the
  // WindowSizer, and we don't want to trigger the Window's built-in "size to
  // default" handling because the browser window has no default preferred
  // size.
  return true;
}

views::View* BrowserView::GetContentsView() {
  return contents_web_view_;
}

views::ClientView* BrowserView::CreateClientView(views::Widget* widget) {
  return this;
}

void BrowserView::OnWidgetDestroying(views::Widget* widget) {
  // Destroy any remaining WebContents early on. Doing so may result in
  // calling back to one of the Views/LayoutManagers or supporting classes of
  // BrowserView. By destroying here we ensure all said classes are valid.
  std::vector<content::WebContents*> contents;
  while (browser()->tab_strip_model()->count())
    contents.push_back(browser()->tab_strip_model()->DetachWebContentsAt(0));
  // Note: The BrowserViewTest tests rely on the contents being destroyed in the
  // order that they were present in the tab strip.
  for (auto* content : contents)
    delete content;
}

void BrowserView::OnWidgetActivationChanged(views::Widget* widget,
                                            bool active) {
  if (active)
    BrowserList::SetLastActive(browser_.get());
  else
    BrowserList::NotifyBrowserNoLongerActive(browser_.get());

  if (!extension_keybinding_registry_ &&
      GetFocusManager()) {  // focus manager can be null in tests.
    extension_keybinding_registry_.reset(new ExtensionKeybindingRegistryViews(
        browser_->profile(), GetFocusManager(),
        extensions::ExtensionKeybindingRegistry::ALL_EXTENSIONS, this));
  }

  extensions::ExtensionCommandsGlobalRegistry* registry =
      extensions::ExtensionCommandsGlobalRegistry::Get(browser_->profile());
  if (active) {
    registry->set_registry_for_active_window(
        extension_keybinding_registry_.get());
  } else if (registry->registry_for_active_window() ==
             extension_keybinding_registry_.get()) {
    registry->set_registry_for_active_window(nullptr);
  }
}

void BrowserView::OnWindowBeginUserBoundsChange() {
  WebContents* web_contents = GetActiveWebContents();
  if (!web_contents)
    return;
  web_contents->GetRenderViewHost()->NotifyMoveOrResizeStarted();
}

void BrowserView::OnWidgetMove() {
  if (!initialized_) {
    // Creating the widget can trigger a move. Ignore it until we've initialized
    // things.
    return;
  }

  // Cancel any tabstrip animations, some of them may be invalidated by the
  // window being repositioned.
  // Comment out for one cycle to see if this fixes dist tests.
  // tabstrip_->DestroyDragController();

  // status_bubble_ may be null if this is invoked during construction.
  if (status_bubble_.get())
    status_bubble_->Reposition();

  BookmarkBubbleView::Hide();

  // Close the omnibox popup, if any.
  LocationBarView* location_bar_view = GetLocationBarView();
  if (location_bar_view)
    location_bar_view->GetOmniboxView()->CloseOmniboxPopup();
}

views::Widget* BrowserView::GetWidget() {
  return View::GetWidget();
}

const views::Widget* BrowserView::GetWidget() const {
  return View::GetWidget();
}

void BrowserView::GetAccessiblePanes(std::vector<views::View*>* panes) {
  // This should be in the order of pane traversal of the panes using F6
  // (Windows) or Ctrl+Back/Forward (Chrome OS).  If one of these is
  // invisible or has no focusable children, it will be automatically
  // skipped.
  panes->push_back(toolbar_);
  if (bookmark_bar_view_.get())
    panes->push_back(bookmark_bar_view_.get());
  if (infobar_container_)
    panes->push_back(infobar_container_);
  if (download_shelf_.get())
    panes->push_back(download_shelf_.get());
  panes->push_back(GetTabContentsContainerView());
  if (devtools_web_view_->visible())
    panes->push_back(devtools_web_view_);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, views::ClientView overrides:

bool BrowserView::CanClose() {
  // You cannot close a frame for which there is an active originating drag
  // session.
  if (tabstrip_ && !tabstrip_->IsTabStripCloseable())
    return false;

  // Give beforeunload handlers the chance to cancel the close before we hide
  // the window below.
  if (!browser_->ShouldCloseWindow())
    return false;

  bool fast_tab_closing_enabled =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableFastUnload);

  if (!browser_->tab_strip_model()->empty()) {
    // Tab strip isn't empty.  Hide the frame (so it appears to have closed
    // immediately) and close all the tabs, allowing the renderers to shut
    // down. When the tab strip is empty we'll be called back again.
    frame_->Hide();
    browser_->OnWindowClosing();
    if (fast_tab_closing_enabled)
      browser_->tab_strip_model()->CloseAllTabs();
    return false;
  } else if (fast_tab_closing_enabled &&
        !browser_->HasCompletedUnloadProcessing()) {
    // The browser needs to finish running unload handlers.
    // Hide the frame (so it appears to have closed immediately), and
    // the browser will call us back again when it is ready to close.
    frame_->Hide();
    return false;
  }

  return true;
}

int BrowserView::NonClientHitTest(const gfx::Point& point) {
  return GetBrowserViewLayout()->NonClientHitTest(point);
}

gfx::Size BrowserView::GetMinimumSize() const {
  return GetBrowserViewLayout()->GetMinimumSize();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, views::View overrides:

const char* BrowserView::GetClassName() const {
  return kViewClassName;
}

void BrowserView::Layout() {
  if (!initialized_ || in_process_fullscreen_)
    return;

  views::View::Layout();

  // TODO(jamescook): Why was this in the middle of layout code?
  toolbar_->location_bar()->omnibox_view()->SetFocusBehavior(
      IsToolbarVisible() ? FocusBehavior::ALWAYS : FocusBehavior::NEVER);
}

void BrowserView::OnGestureEvent(ui::GestureEvent* event) {
  int command;
  if (GetGestureCommand(event, &command) &&
      chrome::IsCommandEnabled(browser(), command)) {
    chrome::ExecuteCommandWithDisposition(
        browser(), command, ui::DispositionFromEventFlags(event->flags()));
    return;
  }

  ClientView::OnGestureEvent(event);
}

void BrowserView::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  if (!initialized_ && details.is_add && details.child == this && GetWidget()) {
    InitViews();
    initialized_ = true;
  }
}

void BrowserView::PaintChildren(const ui::PaintContext& context) {
  views::ClientView::PaintChildren(context);
  // Don't reset the instance before it had a chance to get compositor callback.
  if (!histogram_helper_) {
    histogram_helper_ = BrowserWindowHistogramHelper::
        MaybeRecordValueAndCreateInstanceOnBrowserPaint(
            GetWidget()->GetCompositor());
  }
}

void BrowserView::ChildPreferredSizeChanged(View* child) {
  Layout();
}

void BrowserView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ui::AX_ROLE_CLIENT;
}

void BrowserView::OnThemeChanged() {
  if (!IsRegularOrGuestSession()) {
    // When the theme changes, the native theme may also change (in incognito,
    // the usage of dark or normal hinges on the browser theme), so we have to
    // propagate both kinds of change.
    base::AutoReset<bool> reset(&handling_theme_changed_, true);
#if defined(USE_AURA)
    ui::NativeThemeDarkAura::instance()->NotifyObservers();
#endif
    ui::NativeTheme::GetInstanceForNativeUi()->NotifyObservers();
  }

  views::View::OnThemeChanged();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ui::AcceleratorTarget overrides:

bool BrowserView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  std::map<ui::Accelerator, int>::const_iterator iter =
      accelerator_table_.find(accelerator);
  DCHECK(iter != accelerator_table_.end());
  int command_id = iter->second;

  if (accelerator.IsRepeat() && !IsCommandRepeatable(command_id))
    return false;

  chrome::BrowserCommandController* controller = browser_->command_controller();
  if (!controller->block_command_execution())
    UpdateAcceleratorMetrics(accelerator, command_id);
  return chrome::ExecuteCommand(browser_.get(), command_id);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, OmniboxPopupModelObserver overrides:
void BrowserView::OnOmniboxPopupShownOrHidden() {
  SetMaxTopArrowHeight(GetMaxTopInfoBarArrowHeight(), infobar_container_);
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, InfoBarContainerDelegate overrides:

SkColor BrowserView::GetInfoBarSeparatorColor() const {
  return GetThemeProvider()->GetColor(
      ThemeProperties::COLOR_TOOLBAR_BOTTOM_SEPARATOR);
}

void BrowserView::InfoBarContainerStateChanged(bool is_animating) {
  ToolbarSizeChanged(is_animating);
}

bool BrowserView::DrawInfoBarArrows(int* x) const {
  if (x) {
    gfx::Point anchor(toolbar_->location_bar()->GetInfoBarAnchorPoint());
    ConvertPointToTarget(toolbar_->location_bar(), this, &anchor);
    *x = anchor.x();
  }
  return true;
}

void BrowserView::InitViews() {
  GetWidget()->AddObserver(this);

  // Stow a pointer to this object onto the window handle so that we can get at
  // it later when all we have is a native view.
  GetWidget()->SetNativeWindowProperty(kBrowserViewKey, this);

  // Stow a pointer to the browser's profile onto the window handle so that we
  // can get it later when all we have is a native view.
  GetWidget()->SetNativeWindowProperty(Profile::kProfileKey,
                                       browser_->profile());

  LoadAccelerators();

  contents_web_view_ = new ContentsWebView(browser_->profile());
  contents_web_view_->set_id(VIEW_ID_TAB_CONTAINER);
  contents_web_view_->SetEmbedFullscreenWidgetMode(true);

  web_contents_close_handler_.reset(
      new WebContentsCloseHandler(contents_web_view_));

  devtools_web_view_ = new views::WebView(browser_->profile());
  devtools_web_view_->set_id(VIEW_ID_DEV_TOOLS_DOCKED);
  devtools_web_view_->SetVisible(false);

  contents_container_ = new views::View();
  contents_container_->set_background(views::Background::CreateSolidBackground(
      GetThemeProvider()->GetColor(ThemeProperties::COLOR_CONTROL_BACKGROUND)));
  contents_container_->AddChildView(devtools_web_view_);
  contents_container_->AddChildView(contents_web_view_);
  contents_container_->SetLayoutManager(new ContentsLayoutManager(
      devtools_web_view_, contents_web_view_));
  AddChildView(contents_container_);
  set_contents_view(contents_container_);

  // Top container holds tab strip and toolbar and lives at the front of the
  // view hierarchy.
  top_container_ = new TopContainerView(this);
  AddChildView(top_container_);

  // TabStrip takes ownership of the controller.
  BrowserTabStripController* tabstrip_controller =
      new BrowserTabStripController(browser_->tab_strip_model(), this);
  tabstrip_ = new TabStrip(tabstrip_controller);
  top_container_->AddChildView(tabstrip_);
  tabstrip_controller->InitFromModel(tabstrip_);

  toolbar_ = new ToolbarView(browser_.get());
  top_container_->AddChildView(toolbar_);
  toolbar_->Init();

  // The infobar container must come after the toolbar so its arrow paints on
  // top.
  infobar_container_ = new InfoBarContainerView(this);
  AddChildView(infobar_container_);

  InitStatusBubble();

  // Create do-nothing view for the sake of controlling the z-order of the find
  // bar widget.
  find_bar_host_view_ = new View();
  AddChildView(find_bar_host_view_);

  immersive_mode_controller_->Init(this);

  BrowserViewLayout* browser_view_layout = new BrowserViewLayout;
  browser_view_layout->Init(new BrowserViewLayoutDelegateImpl(this),
                            browser(),
                            this,
                            top_container_,
                            tabstrip_,
                            toolbar_,
                            infobar_container_,
                            contents_container_,
                            GetContentsLayoutManager(),
                            immersive_mode_controller_.get());
  SetLayoutManager(browser_view_layout);

#if defined(OS_WIN)
  // Create a custom JumpList and add it to an observer of TabRestoreService
  // so we can update the custom JumpList when a tab is added or removed.
  if (JumpList::Enabled()) {
    load_complete_listener_.reset(new LoadCompleteListener(this));
  }
#endif

  GetLocationBar()->GetOmniboxView()->model()->popup_model()->AddObserver(this);

  frame_->OnBrowserViewInitViewsComplete();
}

void BrowserView::LoadingAnimationCallback() {
  if (browser_->is_type_tabbed()) {
    // Loading animations are shown in the tab for tabbed windows.  We check the
    // browser type instead of calling IsTabStripVisible() because the latter
    // will return false for fullscreen windows, but we still need to update
    // their animations (so that when they come out of fullscreen mode they'll
    // be correct).
    tabstrip_->UpdateLoadingAnimations();
  } else if (ShouldShowWindowIcon()) {
    // ... or in the window icon area for popups and app windows.
    WebContents* web_contents =
        browser_->tab_strip_model()->GetActiveWebContents();
    // GetActiveWebContents can return null for example under Purify when
    // the animations are running slowly and this function is called on a timer
    // through LoadingAnimationCallback.
    frame_->UpdateThrobber(web_contents && web_contents->IsLoading());
  }
}

void BrowserView::OnLoadCompleted() {
#if defined(OS_WIN)
  DCHECK(!jumplist_.get());
  jumplist_ = JumpListFactory::GetForProfile(browser_->profile());
#endif
}

BrowserViewLayout* BrowserView::GetBrowserViewLayout() const {
  return static_cast<BrowserViewLayout*>(GetLayoutManager());
}

ContentsLayoutManager* BrowserView::GetContentsLayoutManager() const {
  return static_cast<ContentsLayoutManager*>(
      contents_container_->GetLayoutManager());
}

bool BrowserView::MaybeShowBookmarkBar(WebContents* contents) {
  bool show_bookmark_bar = contents &&
      browser_->SupportsWindowFeature(Browser::FEATURE_BOOKMARKBAR);
  if (!show_bookmark_bar && !bookmark_bar_view_.get())
    return false;
  if (!bookmark_bar_view_.get()) {
    bookmark_bar_view_.reset(new BookmarkBarView(browser_.get(), this));
    bookmark_bar_view_->set_owned_by_client();
    bookmark_bar_view_->set_background(
        new BookmarkBarViewBackground(this, bookmark_bar_view_.get()));
    bookmark_bar_view_->SetBookmarkBarState(
        browser_->bookmark_bar_state(),
        BookmarkBar::DONT_ANIMATE_STATE_CHANGE);
    GetBrowserViewLayout()->set_bookmark_bar(bookmark_bar_view_.get());
  }
  // Don't change the visibility of the BookmarkBarView. BrowserViewLayout
  // handles it.
  bookmark_bar_view_->SetPageNavigator(GetActiveWebContents());

  // Update parenting for the bookmark bar. This may detach it from all views.
  bool needs_layout = false;
  views::View* new_parent = nullptr;
  if (show_bookmark_bar) {
    if (bookmark_bar_view_->IsDetached())
      new_parent = this;
    else
      new_parent = top_container_;
  }
  if (new_parent != bookmark_bar_view_->parent()) {
    SetBookmarkBarParent(new_parent);
    needs_layout = true;
  }

  // Check for updates to the desired size.
  if (bookmark_bar_view_->GetPreferredSize().height() !=
      bookmark_bar_view_->height())
    needs_layout = true;

  return needs_layout;
}

void BrowserView::SetBookmarkBarParent(views::View* new_parent) {
  // Because children are drawn in order, the child order also affects z-order:
  // earlier children will appear "below" later ones.  This is important for ink
  // drops, which are drawn with the z-order of the view that parents them.  Ink
  // drops in the toolbar can spread beyond the toolbar bounds, so if the
  // bookmark bar is attached, we want it to be below the toolbar so the toolbar
  // ink drops draw atop it.  This doesn't cause a problem for interactions with
  // the bookmark bar, since it does not host any ink drops that spread beyond
  // its bounds.  If it did, we would need to change how ink drops are drawn.
  // TODO(bruthig): Consider a more general mechanism for manipulating the
  // z-order of the ink drops.

  if (new_parent == this) {
    // BookmarkBarView is detached.
    const int top_container_index = GetIndexOf(top_container_);
    DCHECK_GE(top_container_index, 0);
    // |top_container_| contains the toolbar, so putting the bookmark bar ahead
    // of it will ensure it's drawn before the toolbar.
    AddChildViewAt(bookmark_bar_view_.get(), top_container_index);
  } else if (new_parent == top_container_) {
    // BookmarkBarView is attached.

    // The toolbar is a child of |top_container_|, so making the bookmark bar
    // the first child ensures it's drawn before the toolbar.
    new_parent->AddChildViewAt(bookmark_bar_view_.get(), 0);
  } else {
    DCHECK(!new_parent);
    // Bookmark bar is being detached from all views because it is hidden.
    bookmark_bar_view_->parent()->RemoveChildView(bookmark_bar_view_.get());
  }
}

bool BrowserView::MaybeShowInfoBar(WebContents* contents) {
  // TODO(beng): Remove this function once the interface between
  //             InfoBarContainer, DownloadShelfView and WebContents and this
  //             view is sorted out.
  return true;
}

void BrowserView::UpdateDevToolsForContents(
    WebContents* web_contents, bool update_devtools_web_contents) {
  DevToolsContentsResizingStrategy strategy;
  WebContents* devtools = DevToolsWindow::GetInTabWebContents(
      web_contents, &strategy);

  if (!devtools_web_view_->web_contents() && devtools &&
      !devtools_focus_tracker_.get()) {
    // Install devtools focus tracker when dev tools window is shown for the
    // first time.
    devtools_focus_tracker_.reset(
        new views::ExternalFocusTracker(devtools_web_view_,
                                        GetFocusManager()));
  }

  // Restore focus to the last focused view when hiding devtools window.
  if (devtools_web_view_->web_contents() && !devtools &&
      devtools_focus_tracker_.get()) {
    devtools_focus_tracker_->FocusLastFocusedExternalView();
    devtools_focus_tracker_.reset();
  }

  // Replace devtools WebContents.
  if (devtools_web_view_->web_contents() != devtools &&
      update_devtools_web_contents) {
    devtools_web_view_->SetWebContents(devtools);
  }

  if (devtools) {
    devtools_web_view_->SetVisible(true);
    GetContentsLayoutManager()->SetContentsResizingStrategy(strategy);
  } else {
    devtools_web_view_->SetVisible(false);
    GetContentsLayoutManager()->SetContentsResizingStrategy(
        DevToolsContentsResizingStrategy());
  }
  contents_container_->Layout();

  if (devtools) {
    // When strategy.hide_inspected_contents() returns true, we are hiding
    // contents_web_view_ behind the devtools_web_view_. Otherwise,
    // contents_web_view_ should be right above the devtools_web_view_.
    int devtools_index = contents_container_->GetIndexOf(devtools_web_view_);
    int contents_index = contents_container_->GetIndexOf(contents_web_view_);
    bool devtools_is_on_top = devtools_index > contents_index;
    if (strategy.hide_inspected_contents() != devtools_is_on_top)
      contents_container_->ReorderChildView(contents_web_view_, devtools_index);
  }
}

void BrowserView::UpdateUIForContents(WebContents* contents) {
  bool needs_layout = MaybeShowBookmarkBar(contents);
  // TODO(jamescook): This function always returns true. Remove it and figure
  // out when layout is actually required.
  needs_layout |= MaybeShowInfoBar(contents);
  if (needs_layout)
    Layout();
}

void BrowserView::ProcessFullscreen(bool fullscreen,
                                    const GURL& url,
                                    ExclusiveAccessBubbleType bubble_type) {
  if (in_process_fullscreen_)
    return;
  in_process_fullscreen_ = true;

  // Reduce jankiness during the following position changes by:
  //   * Hiding the window until it's in the final position
  //   * Ignoring all intervening Layout() calls, which resize the webpage and
  //     thus are slow and look ugly (enforced via |in_process_fullscreen_|).
  if (fullscreen) {
    // Move focus out of the location bar if necessary.
    views::FocusManager* focus_manager = GetFocusManager();
    DCHECK(focus_manager);
    // Look for focus in the location bar itself or any child view.
    if (GetLocationBarView()->Contains(focus_manager->GetFocusedView()))
      focus_manager->ClearFocus();
  } else {
    // Hide the fullscreen bubble as soon as possible, since the mode toggle can
    // take enough time for the user to notice.
    exclusive_access_bubble_.reset();
  }

  // Toggle fullscreen mode.
  frame_->SetFullscreen(fullscreen);

  // Enable immersive before the browser refreshes its list of enabled commands.
  if (ShouldUseImmersiveFullscreenForUrl(url))
    immersive_mode_controller_->SetEnabled(fullscreen);

  browser_->WindowFullscreenStateWillChange();
  browser_->WindowFullscreenStateChanged();

  if (fullscreen && !chrome::IsRunningInAppMode()) {
    UpdateExclusiveAccessExitBubbleContent(url, bubble_type);
  }

  // Undo our anti-jankiness hacks and force a re-layout. We also need to
  // recompute the height of the infobar top arrow because toggling in and out
  // of fullscreen changes it. Calling ToolbarSizeChanged() will do both these
  // things since it computes the arrow height directly and forces a layout
  // indirectly via UpdateUIForContents(). Reset |in_process_fullscreen_| in
  // order to let the layout occur.
  in_process_fullscreen_ = false;
  ToolbarSizeChanged(false);

  WebContents* contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (contents && PermissionRequestManager::FromWebContents(contents))
    PermissionRequestManager::FromWebContents(contents)->UpdateAnchorPosition();
}

bool BrowserView::ShouldUseImmersiveFullscreenForUrl(const GURL& url) const {
#if defined(OS_CHROMEOS)
  // Kiosk mode needs the whole screen.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kKioskMode))
    return false;

  return url.is_empty();
#else
  // No immersive except in Chrome OS.
  return false;
#endif
}

void BrowserView::LoadAccelerators() {
  views::FocusManager* focus_manager = GetFocusManager();
  DCHECK(focus_manager);

  // Let's fill our own accelerator table.
  const bool is_app_mode = chrome::IsRunningInForcedAppMode();
  const std::vector<AcceleratorMapping> accelerator_list(GetAcceleratorList());
  for (const auto& entry : accelerator_list) {
    // In app mode, only allow accelerators of white listed commands to pass
    // through.
    if (is_app_mode && !chrome::IsCommandAllowedInAppMode(entry.command_id))
      continue;

    ui::Accelerator accelerator(entry.keycode, entry.modifiers);
    accelerator_table_[accelerator] = entry.command_id;

    // Also register with the focus manager.
    focus_manager->RegisterAccelerator(
        accelerator, ui::AcceleratorManager::kNormalPriority, this);
  }
}

int BrowserView::GetCommandIDForAppCommandID(int app_command_id) const {
#if defined(OS_WIN)
  switch (app_command_id) {
    // NOTE: The order here matches the APPCOMMAND declaration order in the
    // Windows headers.
    case APPCOMMAND_BROWSER_BACKWARD: return IDC_BACK;
    case APPCOMMAND_BROWSER_FORWARD:  return IDC_FORWARD;
    case APPCOMMAND_BROWSER_REFRESH:  return IDC_RELOAD;
    case APPCOMMAND_BROWSER_HOME:     return IDC_HOME;
    case APPCOMMAND_BROWSER_STOP:     return IDC_STOP;
    case APPCOMMAND_BROWSER_SEARCH:   return IDC_FOCUS_SEARCH;
    case APPCOMMAND_HELP:             return IDC_HELP_PAGE_VIA_KEYBOARD;
    case APPCOMMAND_NEW:              return IDC_NEW_TAB;
    case APPCOMMAND_OPEN:             return IDC_OPEN_FILE;
    case APPCOMMAND_CLOSE:            return IDC_CLOSE_TAB;
    case APPCOMMAND_SAVE:             return IDC_SAVE_PAGE;
    case APPCOMMAND_PRINT:            return IDC_PRINT;
    case APPCOMMAND_COPY:             return IDC_COPY;
    case APPCOMMAND_CUT:              return IDC_CUT;
    case APPCOMMAND_PASTE:            return IDC_PASTE;

      // TODO(pkasting): http://b/1113069 Handle these.
    case APPCOMMAND_UNDO:
    case APPCOMMAND_REDO:
    case APPCOMMAND_SPELL_CHECK:
    default:                          return -1;
  }
#else
  // App commands are Windows-specific so there's nothing to do here.
  return -1;
#endif
}

void BrowserView::UpdateAcceleratorMetrics(const ui::Accelerator& accelerator,
                                           int command_id) {
  const ui::KeyboardCode key_code = accelerator.key_code();
  if (command_id == IDC_HELP_PAGE_VIA_KEYBOARD && key_code == ui::VKEY_F1)
    base::RecordAction(UserMetricsAction("ShowHelpTabViaF1"));

  if (command_id == IDC_BOOKMARK_PAGE)
    UMA_HISTOGRAM_ENUMERATION("Bookmarks.EntryPoint",
                              BOOKMARK_ENTRY_POINT_ACCELERATOR,
                              BOOKMARK_ENTRY_POINT_LIMIT);

#if defined(OS_CHROMEOS)
  // Collect information about the relative popularity of various accelerators
  // on Chrome OS.
  switch (command_id) {
    case IDC_BACK:
      if (key_code == ui::VKEY_BROWSER_BACK)
        base::RecordAction(UserMetricsAction("Accel_Back_F1"));
      else if (key_code == ui::VKEY_LEFT)
        base::RecordAction(UserMetricsAction("Accel_Back_Left"));
      break;
    case IDC_FORWARD:
      if (key_code == ui::VKEY_BROWSER_FORWARD)
        base::RecordAction(UserMetricsAction("Accel_Forward_F2"));
      else if (key_code == ui::VKEY_RIGHT)
        base::RecordAction(UserMetricsAction("Accel_Forward_Right"));
      break;
    case IDC_RELOAD:
    case IDC_RELOAD_BYPASSING_CACHE:
      if (key_code == ui::VKEY_R)
        base::RecordAction(UserMetricsAction("Accel_Reload_R"));
      else if (key_code == ui::VKEY_BROWSER_REFRESH)
        base::RecordAction(UserMetricsAction("Accel_Reload_F3"));
      break;
    case IDC_FOCUS_LOCATION:
      if (key_code == ui::VKEY_D)
        base::RecordAction(UserMetricsAction("Accel_FocusLocation_D"));
      else if (key_code == ui::VKEY_L)
        base::RecordAction(UserMetricsAction("Accel_FocusLocation_L"));
      break;
    case IDC_FOCUS_SEARCH:
      if (key_code == ui::VKEY_E)
        base::RecordAction(UserMetricsAction("Accel_FocusSearch_E"));
      else if (key_code == ui::VKEY_K)
        base::RecordAction(UserMetricsAction("Accel_FocusSearch_K"));
      break;
    default:
      // Do nothing.
      break;
  }
#endif
}

void BrowserView::ShowAvatarBubbleFromAvatarButton(
    AvatarBubbleMode mode,
    const signin::ManageAccountsParams& manage_accounts_params,
    signin_metrics::AccessPoint access_point,
    bool focus_first_profile_button) {
#if !defined(OS_CHROMEOS)
  // Do not show avatar bubble if there is no avatar menu button.
  if (!frame_->GetNewAvatarMenuButton())
    return;

  profiles::BubbleViewMode bubble_view_mode;
  profiles::TutorialMode tutorial_mode;
  profiles::BubbleViewModeFromAvatarBubbleMode(mode, &bubble_view_mode,
                                               &tutorial_mode);
  if (SigninViewController::ShouldShowModalSigninForMode(bubble_view_mode)) {
    browser_->signin_view_controller()->ShowModalSignin(
        bubble_view_mode, browser_.get(), access_point);
  } else {
    ProfileChooserView::ShowBubble(bubble_view_mode, tutorial_mode,
                                   manage_accounts_params, access_point,
                                   frame_->GetNewAvatarMenuButton(), browser(),
                                   focus_first_profile_button);
    ProfileMetrics::LogProfileOpenMethod(ProfileMetrics::ICON_AVATAR_BUBBLE);
  }
#else
  NOTREACHED();
#endif
}

int BrowserView::GetRenderViewHeightInsetWithDetachedBookmarkBar() {
  if (browser_->bookmark_bar_state() != BookmarkBar::DETACHED ||
      !bookmark_bar_view_ || !bookmark_bar_view_->IsDetached()) {
    return 0;
  }
  // Don't use bookmark_bar_view_->height() which won't be the final height if
  // the bookmark bar is animating.
  return chrome::kNTPBookmarkBarHeight -
         views::NonClientFrameView::kClientEdgeThickness;
}

void BrowserView::ExecuteExtensionCommand(
    const extensions::Extension* extension,
    const extensions::Command& command) {
  extension_keybinding_registry_->ExecuteCommand(extension->id(),
                                                 command.accelerator());
}

ExclusiveAccessContext* BrowserView::GetExclusiveAccessContext() {
  return this;
}

void BrowserView::ShowImeWarningBubble(
    const extensions::Extension* extension,
    const base::Callback<void(ImeWarningBubblePermissionStatus status)>&
        callback) {
  ImeWarningBubbleView::ShowBubble(extension, this, callback);
}

std::string BrowserView::GetWorkspace() const {
  return frame_->GetWorkspace();
}

bool BrowserView::IsVisibleOnAllWorkspaces() const {
  return frame_->IsVisibleOnAllWorkspaces();
}

bool BrowserView::DoCutCopyPasteForWebContents(
    WebContents* contents,
    void (WebContents::*method)()) {
  // It's possible for a non-null WebContents to have a null RWHV if it's
  // crashed or otherwise been killed.
  content::RenderWidgetHostView* rwhv = contents->GetRenderWidgetHostView();
  if (!rwhv || !rwhv->HasFocus())
    return false;
  // Calling |method| rather than using a fake key event is important since a
  // fake event might be consumed by the web content.
  (contents->*method)();
  return true;
}

void BrowserView::ActivateAppModalDialog() const {
  // If another browser is app modal, flash and activate the modal browser.
  app_modal::AppModalDialog* active_dialog =
      app_modal::AppModalDialogQueue::GetInstance()->active_dialog();
  if (!active_dialog)
    return;

  Browser* modal_browser =
      chrome::FindBrowserWithWebContents(active_dialog->web_contents());
  if (modal_browser && (browser_.get() != modal_browser)) {
    modal_browser->window()->FlashFrame(true);
    modal_browser->window()->Activate();
  }

  app_modal::AppModalDialogQueue::GetInstance()->ActivateModalDialog();
}

int BrowserView::GetMaxTopInfoBarArrowHeight() {
  int top_arrow_height = 0;
  // Only show the arrows when not in fullscreen and when there's no omnibox
  // popup.
  if (!IsFullscreen() &&
      !GetLocationBar()->GetOmniboxView()->model()->popup_model()->IsOpen()) {
    gfx::Point icon_bottom(toolbar_->location_bar()->GetInfoBarAnchorPoint());
    ConvertPointToTarget(toolbar_->location_bar(), this, &icon_bottom);
    gfx::Point infobar_top;
    ConvertPointToTarget(infobar_container_, this, &infobar_top);
    top_arrow_height = infobar_top.y() - icon_bottom.y();
  }
  return top_arrow_height;
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ExclusiveAccessContext implementation:
Profile* BrowserView::GetProfile() {
  return browser_->profile();
}

WebContents* BrowserView::GetActiveWebContents() {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

void BrowserView::UnhideDownloadShelf() {
  if (download_shelf_)
    download_shelf_->Unhide();
}

void BrowserView::HideDownloadShelf() {
  if (download_shelf_)
    download_shelf_->Hide();

  StatusBubble* status_bubble = GetStatusBubble();
  if (status_bubble)
    status_bubble->Hide();
}

///////////////////////////////////////////////////////////////////////////////
// BrowserView, ExclusiveAccessBubbleViewsContext implementation:
ExclusiveAccessManager* BrowserView::GetExclusiveAccessManager() {
  return browser_->exclusive_access_manager();
}

views::Widget* BrowserView::GetBubbleAssociatedWidget() {
  return GetWidget();
}

ui::AcceleratorProvider* BrowserView::GetAcceleratorProvider() {
  return this;
}

gfx::NativeView BrowserView::GetBubbleParentView() const {
  return GetWidget()->GetNativeView();
}

gfx::Point BrowserView::GetCursorPointInParent() const {
  gfx::Point cursor_pos = display::Screen::GetScreen()->GetCursorScreenPoint();
  views::View::ConvertPointFromScreen(GetWidget()->GetRootView(), &cursor_pos);
  return cursor_pos;
}

gfx::Rect BrowserView::GetClientAreaBoundsInScreen() const {
  return GetWidget()->GetClientAreaBoundsInScreen();
}

bool BrowserView::IsImmersiveModeEnabled() {
  return immersive_mode_controller()->IsEnabled();
}

gfx::Rect BrowserView::GetTopContainerBoundsInScreen() {
  return top_container_->GetBoundsInScreen();
}

void BrowserView::DestroyAnyExclusiveAccessBubble() {
  exclusive_access_bubble_.reset();
  new_back_shortcut_bubble_.reset();
}

extensions::ActiveTabPermissionGranter*
BrowserView::GetActiveTabPermissionGranter() {
  content::WebContents* web_contents = GetActiveWebContents();
  if (!web_contents)
    return nullptr;
  return extensions::TabHelper::FromWebContents(web_contents)
      ->active_tab_permission_granter();
}
