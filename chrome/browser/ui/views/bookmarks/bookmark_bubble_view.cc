// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/bookmarks/bookmark_bubble_view.h"

#include <utility>

#include "base/macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/string16.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_bubble_observer.h"
#include "chrome/browser/ui/bookmarks/bookmark_editor.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/sync/sync_promo_ui.h"
#include "chrome/browser/ui/views/harmony/chrome_layout_provider.h"
#include "chrome/browser/ui/views/sync/bubble_sync_promo_view.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/strings/grit/components_strings.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/widget/widget.h"

#if defined(OS_WIN)
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/views/desktop_ios_promotion/desktop_ios_promotion_bubble_view.h"
#include "components/browser_sync/profile_sync_service.h"
#endif

using base::UserMetricsAction;
using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
using views::ColumnSet;
using views::GridLayout;

namespace {

// This combobox prevents any lengthy content from stretching the bubble view.
class UnsizedCombobox : public views::Combobox {
 public:
  explicit UnsizedCombobox(ui::ComboboxModel* model) : views::Combobox(model) {}
  ~UnsizedCombobox() override {}

  gfx::Size GetPreferredSize() const override {
    return gfx::Size(0, views::Combobox::GetPreferredSize().height());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(UnsizedCombobox);
};

}  // namespace

BookmarkBubbleView* BookmarkBubbleView::bookmark_bubble_ = NULL;

// static
views::Widget* BookmarkBubbleView::ShowBubble(
    views::View* anchor_view,
    const gfx::Rect& anchor_rect,
    gfx::NativeView parent_window,
    bookmarks::BookmarkBubbleObserver* observer,
    std::unique_ptr<BubbleSyncPromoDelegate> delegate,
    Profile* profile,
    const GURL& url,
    bool already_bookmarked) {
  if (bookmark_bubble_)
    return nullptr;

  bookmark_bubble_ =
      new BookmarkBubbleView(anchor_view, observer, std::move(delegate),
                             profile, url, !already_bookmarked);
  // Bookmark bubble should always anchor TOP_RIGHT, but the
  // LocationBarBubbleDelegateView does not know that and may use different
  // arrow anchoring.
  bookmark_bubble_->set_arrow(views::BubbleBorder::TOP_RIGHT);
  if (!anchor_view) {
    bookmark_bubble_->SetAnchorRect(anchor_rect);
    bookmark_bubble_->set_parent_window(parent_window);
  }
  views::Widget* bubble_widget =
      views::BubbleDialogDelegateView::CreateBubble(bookmark_bubble_);
  bubble_widget->Show();
  // Select the entire title textfield contents when the bubble is first shown.
  bookmark_bubble_->title_tf_->SelectAll(true);
  bookmark_bubble_->SetArrowPaintType(views::BubbleBorder::PAINT_TRANSPARENT);

  if (bookmark_bubble_->observer_) {
    BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile);
    const BookmarkNode* node = model->GetMostRecentlyAddedUserNodeForURL(url);
    bookmark_bubble_->observer_->OnBookmarkBubbleShown(node);
  }
  return bubble_widget;
}

void BookmarkBubbleView::Hide() {
  if (bookmark_bubble_)
    bookmark_bubble_->GetWidget()->Close();
}

BookmarkBubbleView::~BookmarkBubbleView() {
  if (apply_edits_) {
    ApplyEdits();
  } else if (remove_bookmark_) {
    BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile_);
    const BookmarkNode* node = model->GetMostRecentlyAddedUserNodeForURL(url_);
    if (node)
      model->Remove(node);
  }
  // |parent_combobox_| needs to be destroyed before |parent_model_| as it
  // uses |parent_model_| in its destructor.
  delete parent_combobox_;
}

void BookmarkBubbleView::WindowClosing() {
  // We have to reset |bubble_| here, not in our destructor, because we'll be
  // destroyed asynchronously and the shown state will be checked before then.
  DCHECK_EQ(bookmark_bubble_, this);
  bookmark_bubble_ = NULL;

  if (observer_)
    observer_->OnBookmarkBubbleHidden();
}

bool BookmarkBubbleView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  ui::KeyboardCode key_code = accelerator.key_code();
  if (key_code == ui::VKEY_RETURN) {
    HandleButtonPressed(close_button_);
    return true;
  }
  if (key_code == ui::VKEY_E && accelerator.IsAltDown()) {
    HandleButtonPressed(edit_button_);
    return true;
  }
  if (key_code == ui::VKEY_R && accelerator.IsAltDown()) {
    HandleButtonPressed(remove_button_);
    return true;
  }

  return LocationBarBubbleDelegateView::AcceleratorPressed(accelerator);
}

void BookmarkBubbleView::Init() {
  remove_button_ = views::MdTextButton::CreateSecondaryUiButton(
      this, l10n_util::GetStringUTF16(IDS_BOOKMARK_BUBBLE_REMOVE_BOOKMARK));

  edit_button_ = views::MdTextButton::CreateSecondaryUiButton(
      this, l10n_util::GetStringUTF16(IDS_BOOKMARK_BUBBLE_OPTIONS));

  close_button_ = views::MdTextButton::CreateSecondaryUiButton(
      this, l10n_util::GetStringUTF16(IDS_DONE));
  close_button_->SetIsDefault(true);

  views::Label* combobox_label = new views::Label(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BUBBLE_FOLDER_TEXT));

  parent_combobox_ = new UnsizedCombobox(&parent_model_);
  parent_combobox_->set_listener(this);
  parent_combobox_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_AX_BUBBLE_FOLDER_TEXT));

  SetLayoutManager(new views::FillLayout);
  bookmark_details_view_ = base::MakeUnique<View>();
  GridLayout* layout = new GridLayout(bookmark_details_view_.get());
  bookmark_details_view_->SetLayoutManager(layout);

  // This column set is used for the labels and textfields as well as the
  // buttons at the bottom.
  const int cs_id = 0;
  ColumnSet* cs = layout->AddColumnSet(cs_id);
  ChromeLayoutProvider* provider = ChromeLayoutProvider::Get();

  cs->AddColumn(provider->GetControlLabelGridAlignment(), GridLayout::CENTER, 0,
                GridLayout::USE_PREF, 0, 0);
  cs->AddPaddingColumn(
      0, provider->GetDistanceMetric(DISTANCE_UNRELATED_CONTROL_HORIZONTAL));

  cs->AddColumn(GridLayout::FILL, GridLayout::CENTER, 0,
                GridLayout::USE_PREF, 0, 0);
  cs->AddPaddingColumn(1, provider->GetDistanceMetric(
                              DISTANCE_UNRELATED_CONTROL_HORIZONTAL_LARGE));

  cs->AddColumn(GridLayout::LEADING, GridLayout::TRAILING, 0,
                GridLayout::USE_PREF, 0, 0);
  cs->AddPaddingColumn(0, provider->GetDistanceMetric(
                              views::DISTANCE_RELATED_BUTTON_HORIZONTAL));
  cs->AddColumn(GridLayout::LEADING, GridLayout::TRAILING, 0,
                GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, cs_id);
  views::Label* label = new views::Label(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_BUBBLE_TITLE_TEXT));
  layout->AddView(label);
  title_tf_ = new views::Textfield();
  title_tf_->SetText(GetTitle());
  title_tf_->SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_BOOKMARK_AX_BUBBLE_TITLE_TEXT));

  layout->AddView(title_tf_, 5, 1);

  layout->AddPaddingRow(0,
                        provider->GetInsetsMetric(views::INSETS_PANEL).top());

  layout->StartRow(0, cs_id);
  layout->AddView(combobox_label);
  layout->AddView(parent_combobox_, 5, 1);

  layout->AddPaddingRow(
      0, provider->GetDistanceMetric(views::DISTANCE_RELATED_CONTROL_VERTICAL));

  layout->StartRow(0, cs_id);
  layout->SkipColumns(2);
  layout->AddView(remove_button_);
  layout->AddView(edit_button_);
  layout->AddView(close_button_);

  AddAccelerator(ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
  AddAccelerator(ui::Accelerator(ui::VKEY_E, ui::EF_ALT_DOWN));
  AddAccelerator(ui::Accelerator(ui::VKEY_R, ui::EF_ALT_DOWN));

  AddChildView(bookmark_details_view_.get());
}

gfx::ImageSkia BookmarkBubbleView::GetWindowIcon() {
#if defined(OS_WIN)
  if (is_showing_ios_promotion_) {
    return desktop_ios_promotion::GetPromoImage(
        GetNativeTheme()->GetSystemColor(
            ui::NativeTheme::kColorId_TextfieldDefaultColor));
  }
#endif
  return gfx::ImageSkia();
}

bool BookmarkBubbleView::ShouldShowWindowIcon() const {
  return is_showing_ios_promotion_;
}

base::string16 BookmarkBubbleView::GetWindowTitle() const {
#if defined(OS_WIN)
  if (is_showing_ios_promotion_) {
    return desktop_ios_promotion::GetPromoTitle(
        desktop_ios_promotion::PromotionEntryPoint::BOOKMARKS_BUBBLE);
  }
#endif
  return l10n_util::GetStringUTF16(newly_bookmarked_
                                       ? IDS_BOOKMARK_BUBBLE_PAGE_BOOKMARKED
                                       : IDS_BOOKMARK_BUBBLE_PAGE_BOOKMARK);
}

const char* BookmarkBubbleView::GetClassName() const {
  return "BookmarkBubbleView";
}

views::View* BookmarkBubbleView::GetInitiallyFocusedView() {
  return title_tf_;
}

views::View* BookmarkBubbleView::CreateFootnoteView() {
  if (!SyncPromoUI::ShouldShowSyncPromo(profile_))
    return nullptr;

  base::RecordAction(
      base::UserMetricsAction("Signin_Impression_FromBookmarkBubble"));

  return new BubbleSyncPromoView(delegate_.get(), IDS_BOOKMARK_SYNC_PROMO_LINK,
                                 IDS_BOOKMARK_SYNC_PROMO_MESSAGE);
}

BookmarkBubbleView::BookmarkBubbleView(
    views::View* anchor_view,
    bookmarks::BookmarkBubbleObserver* observer,
    std::unique_ptr<BubbleSyncPromoDelegate> delegate,
    Profile* profile,
    const GURL& url,
    bool newly_bookmarked)
    : LocationBarBubbleDelegateView(anchor_view, nullptr),
      observer_(observer),
      delegate_(std::move(delegate)),
      profile_(profile),
      url_(url),
      newly_bookmarked_(newly_bookmarked),
      parent_model_(BookmarkModelFactory::GetForBrowserContext(profile_),
                    BookmarkModelFactory::GetForBrowserContext(profile_)
                        ->GetMostRecentlyAddedUserNodeForURL(url)),
      remove_button_(nullptr),
      edit_button_(nullptr),
      close_button_(nullptr),
      title_tf_(nullptr),
      parent_combobox_(nullptr),
      ios_promo_view_(nullptr),
      remove_bookmark_(false),
      apply_edits_(true),
      is_showing_ios_promotion_(false) {
  chrome::RecordDialogCreation(chrome::DialogIdentifier::BOOKMARK);
}

base::string16 BookmarkBubbleView::GetTitle() {
  BookmarkModel* bookmark_model =
      BookmarkModelFactory::GetForBrowserContext(profile_);
  const BookmarkNode* node =
      bookmark_model->GetMostRecentlyAddedUserNodeForURL(url_);
  if (node)
    return node->GetTitle();
  else
    NOTREACHED();
  return base::string16();
}

void BookmarkBubbleView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  LocationBarBubbleDelegateView::GetAccessibleNodeData(node_data);
  node_data->SetName(l10n_util::GetStringUTF8(
      newly_bookmarked_ ? IDS_BOOKMARK_BUBBLE_PAGE_BOOKMARKED
                        : IDS_BOOKMARK_AX_BUBBLE_PAGE_BOOKMARK));
}

void BookmarkBubbleView::ButtonPressed(views::Button* sender,
                                       const ui::Event& event) {
  HandleButtonPressed(sender);
}

void BookmarkBubbleView::OnPerformAction(views::Combobox* combobox) {
  if (combobox->selected_index() + 1 == parent_model_.GetItemCount()) {
    base::RecordAction(UserMetricsAction("BookmarkBubble_EditFromCombobox"));
    ShowEditor();
  }
}

void BookmarkBubbleView::HandleButtonPressed(views::Button* sender) {
  if (sender == remove_button_) {
    base::RecordAction(UserMetricsAction("BookmarkBubble_Unstar"));
    // Set this so we remove the bookmark after the window closes.
    remove_bookmark_ = true;
    apply_edits_ = false;
    GetWidget()->Close();
  } else if (sender == edit_button_) {
    base::RecordAction(UserMetricsAction("BookmarkBubble_Edit"));
    ShowEditor();
  } else {
    DCHECK_EQ(close_button_, sender);
#if defined(OS_WIN)
    PrefService* prefs = profile_->GetPrefs();
    const browser_sync::ProfileSyncService* sync_service =
        ProfileSyncServiceFactory::GetForProfile(profile_);
    if (desktop_ios_promotion::IsEligibleForIOSPromotion(
            prefs, sync_service,
            desktop_ios_promotion::PromotionEntryPoint::BOOKMARKS_BUBBLE)) {
      ShowIOSPromotion();
    } else {
      GetWidget()->Close();
    }
#else
    GetWidget()->Close();
#endif
  }
}

void BookmarkBubbleView::ShowEditor() {
  const BookmarkNode* node =
      BookmarkModelFactory::GetForBrowserContext(profile_)
          ->GetMostRecentlyAddedUserNodeForURL(url_);
  gfx::NativeWindow native_parent =
      anchor_widget() ? anchor_widget()->GetNativeWindow()
                      : platform_util::GetTopLevel(parent_window());
  DCHECK(native_parent);

  Profile* profile = profile_;
  ApplyEdits();
  GetWidget()->Close();

  if (node && native_parent)
    BookmarkEditor::Show(native_parent, profile,
                         BookmarkEditor::EditDetails::EditNode(node),
                         BookmarkEditor::SHOW_TREE);
}

void BookmarkBubbleView::ApplyEdits() {
  // Set this to make sure we don't attempt to apply edits again.
  apply_edits_ = false;

  BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile_);
  const BookmarkNode* node = model->GetMostRecentlyAddedUserNodeForURL(url_);
  if (node) {
    const base::string16 new_title = title_tf_->text();
    if (new_title != node->GetTitle()) {
      model->SetTitle(node, new_title);
      base::RecordAction(
          UserMetricsAction("BookmarkBubble_ChangeTitleInBubble"));
    }
    parent_model_.MaybeChangeParent(node, parent_combobox_->selected_index());
  }
}

#if defined(OS_WIN)
void BookmarkBubbleView::ShowIOSPromotion() {
  DCHECK(!is_showing_ios_promotion_);
  RemoveChildView(bookmark_details_view_.get());
  is_showing_ios_promotion_ = true;
  ios_promo_view_ = new DesktopIOSPromotionBubbleView(
      profile_, desktop_ios_promotion::PromotionEntryPoint::BOOKMARKS_BUBBLE);
  AddChildView(ios_promo_view_);
  GetWidget()->UpdateWindowIcon();
  GetWidget()->UpdateWindowTitle();
  // Resize the bubble so it has the same width as the parent bubble.
  ios_promo_view_->UpdateBubbleHeight();
}
#endif
