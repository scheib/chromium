// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/accessibility/invert_bubble_view.h"

#include "base/macros.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/harmony/chrome_layout_provider.h"
#include "chrome/browser/ui/views/toolbar/app_menu_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/page_navigator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/bubble/bubble_dialog_delegate.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/link_listener.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/widget/widget.h"

namespace {

const char kHighContrastExtensionUrl[] =
    "https://chrome.google.com/webstore/detail/djcfdncoelnlbldjfhinnjlhdjlikmph";
const char kDarkThemeSearchUrl[] =
    "https://chrome.google.com/webstore/search-themes/dark";
const char kLearnMoreUrl[] =
    "https://groups.google.com/a/googleproductforums.com/d/topic/chrome/Xrco2HsXS-8/discussion";

class InvertBubbleView : public views::BubbleDialogDelegateView,
                         public views::LinkListener {
 public:
  InvertBubbleView(Browser* browser, views::View* anchor_view);
  ~InvertBubbleView() override;

 private:
  // Overridden from views::BubbleDialogDelegateView:
  int GetDialogButtons() const override;
  void Init() override;

  // Overridden from views::LinkListener:
  void LinkClicked(views::Link* source, int event_flags) override;

  void OpenLink(const std::string& url, int event_flags);

  Browser* browser_;
  views::Link* high_contrast_;
  views::Link* dark_theme_;
  views::Link* learn_more_;
  views::Link* close_;

  DISALLOW_COPY_AND_ASSIGN(InvertBubbleView);
};

InvertBubbleView::InvertBubbleView(Browser* browser, views::View* anchor_view)
    : views::BubbleDialogDelegateView(anchor_view,
                                      views::BubbleBorder::TOP_RIGHT),
      browser_(browser),
      high_contrast_(NULL),
      dark_theme_(NULL),
      learn_more_(NULL),
      close_(NULL) {
  chrome::RecordDialogCreation(chrome::DialogIdentifier::INVERT);
}

InvertBubbleView::~InvertBubbleView() {
}

int InvertBubbleView::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_NONE;
}

void InvertBubbleView::Init() {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  const gfx::FontList& original_font_list =
      rb.GetFontList(ui::ResourceBundle::MediumFont);

  // TODO(tapted): This should be using WidgetDelegate::GetWindowTitle().
  views::Label* title = new views::Label(
      base::string16(), views::Label::CustomFont{original_font_list.Derive(
                            2, gfx::Font::NORMAL, gfx::Font::Weight::BOLD)});
  title->SetMultiLine(true);

  learn_more_ = new views::Link(l10n_util::GetStringUTF16(IDS_LEARN_MORE));
  learn_more_->SetFontList(original_font_list);
  learn_more_->set_listener(this);

  high_contrast_ =
      new views::Link(l10n_util::GetStringUTF16(IDS_HIGH_CONTRAST_EXT));
  high_contrast_->SetFontList(original_font_list);
  high_contrast_->set_listener(this);

  dark_theme_ = new views::Link(l10n_util::GetStringUTF16(IDS_DARK_THEME));
  dark_theme_->SetFontList(original_font_list);
  dark_theme_->set_listener(this);

  close_ = new views::Link(l10n_util::GetStringUTF16(IDS_CLOSE));
  close_->SetFontList(original_font_list);
  close_->set_listener(this);

  views::GridLayout* layout = views::GridLayout::CreatePanel(this);

  views::ColumnSet* columns = layout->AddColumnSet(0);
  for (int i = 0; i < 4; i++) {
    columns->AddColumn(views::GridLayout::LEADING,
                       views::GridLayout::LEADING, 0,
                       views::GridLayout::USE_PREF, 0, 0);
  }

  layout->StartRow(0, 0);
  layout->AddView(title, 4, 1);
  layout->StartRowWithPadding(0, 0, 0,
                              ChromeLayoutProvider::Get()->GetDistanceMetric(
                                  DISTANCE_RELATED_CONTROL_VERTICAL_SMALL));
  layout->AddView(high_contrast_);
  layout->AddView(dark_theme_);
  layout->AddView(learn_more_);
  layout->AddView(close_);

  // Fit the message to the width of the links in the bubble.
  const gfx::Size size(GetPreferredSize());
  title->SetText(l10n_util::GetStringUTF16(IDS_HIGH_CONTRAST_NOTIFICATION));
  title->SizeToFit(size.width());

  // Switching to high-contrast mode has a nasty habit of causing Chrome
  // top-level windows to lose focus, so closing the bubble on deactivate
  // makes it disappear before the user has even seen it. This forces the
  // user to close it explicitly, which should be okay because it affects
  // a small minority of users, and only once.
  set_close_on_deactivate(false);
}

void InvertBubbleView::LinkClicked(views::Link* source, int event_flags) {
  if (source == high_contrast_)
    OpenLink(kHighContrastExtensionUrl, event_flags);
  else if (source == dark_theme_)
    OpenLink(kDarkThemeSearchUrl, event_flags);
  else if (source == learn_more_)
    OpenLink(kLearnMoreUrl, event_flags);
  else if (source == close_)
    GetWidget()->Close();
  else
    NOTREACHED();
}

void InvertBubbleView::OpenLink(const std::string& url, int event_flags) {
  WindowOpenDisposition disposition =
      ui::DispositionFromEventFlags(event_flags);
  content::OpenURLParams params(
      GURL(url), content::Referrer(),
      disposition == WindowOpenDisposition::CURRENT_TAB
          ? WindowOpenDisposition::NEW_FOREGROUND_TAB
          : disposition,
      ui::PAGE_TRANSITION_LINK, false);
  browser_->OpenURL(params);
}

}  // namespace

namespace chrome {

void MaybeShowInvertBubbleView(BrowserView* browser_view) {
  Browser* browser = browser_view->browser();
  PrefService* pref_service = browser->profile()->GetPrefs();
  views::View* anchor = browser_view->toolbar()->app_menu_button();
  if (color_utils::IsInvertedColorScheme() && anchor && anchor->GetWidget() &&
      !pref_service->GetBoolean(prefs::kInvertNotificationShown)) {
    pref_service->SetBoolean(prefs::kInvertNotificationShown, true);
    InvertBubbleView* delegate = new InvertBubbleView(browser, anchor);
    views::BubbleDialogDelegateView::CreateBubble(delegate)->Show();
  }
}

}  // namespace chrome
