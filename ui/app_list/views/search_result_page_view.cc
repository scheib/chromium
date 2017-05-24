// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/views/search_result_page_view.h"

#include <stddef.h>

#include <algorithm>

#include "base/memory/ptr_util.h"
#include "ui/app_list/app_list_constants.h"
#include "ui/app_list/app_list_features.h"
#include "ui/app_list/app_list_view_delegate.h"
#include "ui/app_list/views/app_list_main_view.h"
#include "ui/app_list/views/search_box_view.h"
#include "ui/app_list/views/search_result_list_view.h"
#include "ui/app_list/views/search_result_tile_item_list_view.h"
#include "ui/gfx/shadow_value.h"
#include "ui/views/background.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/shadow_border.h"

namespace app_list {

namespace {

const int kGroupSpacing = 6;
const int kTopPadding = 8;

// The z-height of the search box and cards in this view.
const int kSearchResultZHeight = 1;

// A container view that ensures the card background and the shadow are painted
// in the correct order.
class SearchCardView : public views::View {
 public:
  explicit SearchCardView(views::View* content_view) {
    SetBorder(base::MakeUnique<views::ShadowBorder>(
        GetShadowForZHeight(kSearchResultZHeight)));
    SetLayoutManager(new views::FillLayout());
    content_view->set_background(
        views::Background::CreateSolidBackground(kCardBackgroundColor));
    AddChildView(content_view);
  }

  ~SearchCardView() override {}
};

}  // namespace

SearchResultPageView::SearchResultPageView() : selected_index_(0) {
  gfx::ShadowValue shadow = GetShadowForZHeight(kSearchResultZHeight);
  std::unique_ptr<views::Border> border(new views::ShadowBorder(shadow));

  gfx::Insets insets =
      gfx::Insets(kTopPadding, kSearchBoxPadding, 0, kSearchBoxPadding);
  insets += -border->GetInsets();

  views::BoxLayout* layout =
      new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, kGroupSpacing);
  layout->set_inside_border_insets(insets);

  SetLayoutManager(layout);
}

SearchResultPageView::~SearchResultPageView() {
}

void SearchResultPageView::SetSelection(bool select) {
  if (select)
    SetSelectedIndex(0, false);
  else
    ClearSelectedIndex();
}

void SearchResultPageView::AddSearchResultContainerView(
    AppListModel::SearchResults* results_model,
    SearchResultContainerView* result_container) {
  AddChildView(new SearchCardView(result_container));
  result_container_views_.push_back(result_container);
  result_container->SetResults(results_model);
  result_container->set_delegate(this);
}

bool SearchResultPageView::OnKeyPressed(const ui::KeyEvent& event) {
  if (HasSelection() &&
      result_container_views_.at(selected_index_)->OnKeyPressed(event)) {
    return true;
  }

  int dir = 0;
  bool directional_movement = false;
  switch (event.key_code()) {
    case ui::VKEY_TAB:
      dir = event.IsShiftDown() ? -1 : 1;
      break;
    case ui::VKEY_UP:
      dir = -1;
      directional_movement = true;
      break;
    case ui::VKEY_DOWN:
      dir = 1;
      directional_movement = true;
      break;
    default:
      return false;
  }

  // Find the next result container with results.
  int new_selected = selected_index_;
  do {
    new_selected += dir;
  } while (IsValidSelectionIndex(new_selected) &&
           result_container_views_[new_selected]->num_results() == 0);

  if (IsValidSelectionIndex(new_selected)) {
    SetSelectedIndex(new_selected, directional_movement);
    return true;
  }

  return false;
}

const char* SearchResultPageView::GetClassName() const {
  return "SearchResultPageView";
}

void SearchResultPageView::ClearSelectedIndex() {
  if (HasSelection())
    result_container_views_[selected_index_]->ClearSelectedIndex();

  selected_index_ = -1;
}

void SearchResultPageView::SetSelectedIndex(int index,
                                            bool directional_movement) {
  bool from_bottom = index < selected_index_;

  // Reset the old selected view's selection.
  ClearSelectedIndex();

  selected_index_ = index;
  // Set the new selected view's selection to its first result.
  result_container_views_[selected_index_]->OnContainerSelected(
      from_bottom, directional_movement);
}

bool SearchResultPageView::IsValidSelectionIndex(int index) {
  return index >= 0 && index < static_cast<int>(result_container_views_.size());
}

void SearchResultPageView::OnSearchResultContainerResultsChanged() {
  DCHECK(!result_container_views_.empty());

  // Only sort and layout the containers when they have all updated.
  for (SearchResultContainerView* view : result_container_views_) {
    if (view->UpdateScheduled()) {
      return;
    }
  }

  SearchResultContainerView* old_selection =
      HasSelection() ? result_container_views_[selected_index_] : nullptr;

  // Truncate the currently selected container's selection if necessary. If
  // there are no results, the selection will be cleared below.
  if (old_selection && old_selection->num_results() > 0 &&
      old_selection->selected_index() >= old_selection->num_results()) {
    old_selection->SetSelectedIndex(old_selection->num_results() - 1);
  }

  // Sort the result container views by their score.
  std::sort(result_container_views_.begin(), result_container_views_.end(),
            [](const SearchResultContainerView* a,
               const SearchResultContainerView* b) -> bool {
              return a->container_score() > b->container_score();
            });

  int result_y_index = 0;
  for (size_t i = 0; i < result_container_views_.size(); ++i) {
    SearchResultContainerView* view = result_container_views_[i];
    ReorderChildView(view->parent(), i);

    view->NotifyFirstResultYIndex(result_y_index);

    result_y_index += view->GetYSize();
  }

  Layout();

  SearchResultContainerView* new_selection = nullptr;
  if (HasSelection() &&
      result_container_views_[selected_index_]->num_results() > 0) {
    new_selection = result_container_views_[selected_index_];
  }

  // If there was no previous selection or the new view at the selection index
  // is different from the old one, update the selected view.
  if (!HasSelection() || old_selection != new_selection) {
    if (old_selection)
      old_selection->ClearSelectedIndex();

    int new_selection_index = new_selection ? selected_index_ : 0;
    // Clear the current selection so that the selection always comes in from
    // the top.
    ClearSelectedIndex();
    SetSelectedIndex(new_selection_index, false);
  }
}

gfx::Rect SearchResultPageView::GetPageBoundsForState(
    AppListModel::State state) const {
  gfx::Rect onscreen_bounds =
      features::IsAnswerCardEnabled() && !features::IsAnswerCardDarkRunEnabled()
          ? GetFullContentsBounds()
          : GetDefaultContentsBounds();
  switch (state) {
    case AppListModel::STATE_SEARCH_RESULTS:
      return onscreen_bounds;
    default:
      return GetAboveContentsOffscreenBounds(onscreen_bounds.size());
  }
}

void SearchResultPageView::OnAnimationUpdated(double progress,
                                              AppListModel::State from_state,
                                              AppListModel::State to_state) {
  if (from_state != AppListModel::STATE_SEARCH_RESULTS &&
      to_state != AppListModel::STATE_SEARCH_RESULTS) {
    return;
  }

  gfx::Rect onscreen_bounds(
      GetPageBoundsForState(AppListModel::STATE_SEARCH_RESULTS));
  onscreen_bounds -= bounds().OffsetFromOrigin();
  gfx::Path path;
  path.addRect(gfx::RectToSkRect(onscreen_bounds));
  set_clip_path(path);
}

int SearchResultPageView::GetSearchBoxZHeight() const {
  return kSearchResultZHeight;
}

void SearchResultPageView::OnHidden() {
  ClearSelectedIndex();
}

}  // namespace app_list
