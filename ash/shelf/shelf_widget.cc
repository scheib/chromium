// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/shelf_widget.h"

#include "ash/animation/animation_change_type.h"
#include "ash/focus_cycler.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/root_window_controller.h"
#include "ash/session/session_controller.h"
#include "ash/shelf/app_list_button.h"
#include "ash/shelf/shelf_background_animator_observer.h"
#include "ash/shelf/shelf_constants.h"
#include "ash/shelf/shelf_layout_manager.h"
#include "ash/shelf/shelf_view.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/system/status_area_layout_manager.h"
#include "ash/system/status_area_widget.h"
#include "ash/wm_window.h"
#include "base/memory/ptr_util.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/skbitmap_operations.h"
#include "ui/views/accessible_pane_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace ash {

// The contents view of the Shelf. This view contains ShelfView and
// sizes it to the width of the shelf minus the size of the status area.
class ShelfWidget::DelegateView : public views::WidgetDelegate,
                                  public views::AccessiblePaneView,
                                  public ShelfBackgroundAnimatorObserver {
 public:
  explicit DelegateView(ShelfWidget* shelf);
  ~DelegateView() override;

  void set_focus_cycler(FocusCycler* focus_cycler) {
    focus_cycler_ = focus_cycler;
  }
  FocusCycler* focus_cycler() { return focus_cycler_; }

  ui::Layer* opaque_background() { return &opaque_background_; }
  ui::Layer* opaque_foreground() { return &opaque_foreground_; }

  void SetParentLayer(ui::Layer* layer);

  // views::WidgetDelegateView overrides:
  views::Widget* GetWidget() override { return View::GetWidget(); }
  const views::Widget* GetWidget() const override { return View::GetWidget(); }

  bool CanActivate() const override;
  void ReorderChildLayers(ui::Layer* parent_layer) override;
  // This will be called when the parent local bounds change.
  void OnBoundsChanged(const gfx::Rect& old_bounds) override;

  // ShelfBackgroundAnimatorObserver:
  void UpdateShelfBackground(SkColor color) override;

 private:
  ShelfWidget* shelf_widget_;
  FocusCycler* focus_cycler_;
  // A black background layer that may be visible depending on a
  // ShelfBackgroundAnimator.
  ui::Layer opaque_background_;
  // A black foreground layer which is shown while transitioning between users.
  // Note: Since the back- and foreground layers have different functions they
  // can be used simultaneously - so no repurposing possible.
  ui::Layer opaque_foreground_;

  DISALLOW_COPY_AND_ASSIGN(DelegateView);
};

ShelfWidget::DelegateView::DelegateView(ShelfWidget* shelf_widget)
    : shelf_widget_(shelf_widget),
      focus_cycler_(nullptr),
      opaque_background_(ui::LAYER_SOLID_COLOR),
      opaque_foreground_(ui::LAYER_SOLID_COLOR) {
  DCHECK(shelf_widget_);
  SetLayoutManager(new views::FillLayout());
  set_allow_deactivate_on_esc(true);
  opaque_background_.SetBounds(GetLocalBounds());
  opaque_foreground_.SetBounds(GetLocalBounds());
  opaque_foreground_.SetOpacity(0.0f);
}

ShelfWidget::DelegateView::~DelegateView() {}

void ShelfWidget::DelegateView::SetParentLayer(ui::Layer* layer) {
  layer->Add(&opaque_background_);
  layer->Add(&opaque_foreground_);
  ReorderLayers();
}

bool ShelfWidget::DelegateView::CanActivate() const {
  // Allow to activate as fallback.
  if (shelf_widget_->activating_as_fallback_)
    return true;
  // Allow to activate from the focus cycler.
  if (focus_cycler_ && focus_cycler_->widget_activating() == GetWidget())
    return true;
  // Disallow activating in other cases, especially when using mouse.
  return false;
}

void ShelfWidget::DelegateView::ReorderChildLayers(ui::Layer* parent_layer) {
  views::View::ReorderChildLayers(parent_layer);
  parent_layer->StackAtBottom(&opaque_background_);
  parent_layer->StackAtTop(&opaque_foreground_);
}

void ShelfWidget::DelegateView::OnBoundsChanged(const gfx::Rect& old_bounds) {
  opaque_background_.SetBounds(GetLocalBounds());
  opaque_foreground_.SetBounds(GetLocalBounds());
}

void ShelfWidget::DelegateView::UpdateShelfBackground(SkColor color) {
  opaque_background_.SetColor(color);
}

ShelfWidget::ShelfWidget(WmWindow* shelf_container, WmShelf* wm_shelf)
    : wm_shelf_(wm_shelf),
      status_area_widget_(nullptr),
      delegate_view_(new DelegateView(this)),
      shelf_view_(new ShelfView(Shell::Get()->shelf_model(), wm_shelf_, this)),
      background_animator_(SHELF_BACKGROUND_DEFAULT,
                           wm_shelf_,
                           Shell::Get()->wallpaper_controller()),
      activating_as_fallback_(false) {
  DCHECK(shelf_container);
  DCHECK(wm_shelf_);

  views::Widget::InitParams params(
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.name = "ShelfWidget";
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.delegate = delegate_view_;
  shelf_container->GetRootWindowController()
      ->ConfigureWidgetInitParamsForContainer(
          this, shelf_container->aura_window()->id(), &params);
  Init(params);

  // The shelf should not take focus when initially shown.
  set_focus_on_creation(false);
  SetContentsView(delegate_view_);
  delegate_view_->SetParentLayer(GetLayer());

  // The shelf view observes the shelf model and creates icons as items are
  // added to the model.
  shelf_view_->Init();
  GetContentsView()->AddChildView(shelf_view_);

  shelf_layout_manager_ = new ShelfLayoutManager(this, wm_shelf_);
  shelf_layout_manager_->AddObserver(this);
  shelf_container->aura_window()->SetLayoutManager(shelf_layout_manager_);
  background_animator_.PaintBackground(
      shelf_layout_manager_->GetShelfBackgroundType(),
      AnimationChangeType::IMMEDIATE);

  views::Widget::AddObserver(this);

  // Calls back into |this| and depends on |shelf_view_|.
  background_animator_.AddObserver(this);
  background_animator_.AddObserver(delegate_view_);
}

ShelfWidget::~ShelfWidget() {
  // Must call Shutdown() before destruction.
  DCHECK(!status_area_widget_);
  background_animator_.RemoveObserver(delegate_view_);
  background_animator_.RemoveObserver(this);
  Shell::Get()->focus_cycler()->RemoveWidget(this);
  SetFocusCycler(nullptr);
  RemoveObserver(this);
}

void ShelfWidget::CreateStatusAreaWidget(WmWindow* status_container) {
  DCHECK(status_container);
  DCHECK(!status_area_widget_);
  status_area_widget_ =
      new StatusAreaWidget(status_container->aura_window(), wm_shelf_);
  status_area_widget_->CreateTrayViews();
  // NOTE: Container may be hidden depending on login/display state.
  status_area_widget_->Show();
  Shell::Get()->focus_cycler()->AddWidget(status_area_widget_);
  background_animator_.AddObserver(status_area_widget_);
  status_container->aura_window()->SetLayoutManager(
      new StatusAreaLayoutManager(this));
}

void ShelfWidget::SetPaintsBackground(ShelfBackgroundType background_type,
                                      AnimationChangeType change_type) {
  background_animator_.PaintBackground(background_type, change_type);
}

ShelfBackgroundType ShelfWidget::GetBackgroundType() const {
  return background_animator_.target_background_type();
}

void ShelfWidget::HideShelfBehindBlackBar(bool hide, int animation_time_ms) {
  if (IsShelfHiddenBehindBlackBar() == hide)
    return;

  ui::Layer* opaque_foreground = delegate_view_->opaque_foreground();
  float target_opacity = hide ? 1.0f : 0.0f;
  std::unique_ptr<ui::ScopedLayerAnimationSettings> opaque_foreground_animation;
  opaque_foreground_animation.reset(
      new ui::ScopedLayerAnimationSettings(opaque_foreground->GetAnimator()));
  opaque_foreground_animation->SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(animation_time_ms));
  opaque_foreground_animation->SetPreemptionStrategy(
      ui::LayerAnimator::REPLACE_QUEUED_ANIMATIONS);

  opaque_foreground->SetOpacity(target_opacity);
}

bool ShelfWidget::IsShelfHiddenBehindBlackBar() const {
  return delegate_view_->opaque_foreground()->GetTargetOpacity() != 0.0f;
}

void ShelfWidget::OnShelfAlignmentChanged() {
  shelf_view_->OnShelfAlignmentChanged();
  status_area_widget_->UpdateAfterShelfAlignmentChange();
  delegate_view_->SchedulePaint();
}

void ShelfWidget::PostCreateShelf() {
  SetFocusCycler(Shell::Get()->focus_cycler());

  // Ensure the newly created |shelf_| gets current values.
  background_animator_.NotifyObserver(this);

  shelf_layout_manager_->LayoutShelf();
  shelf_layout_manager_->UpdateAutoHideState();
  Show();
}

bool ShelfWidget::IsShowingAppList() const {
  return GetAppListButton() && GetAppListButton()->is_showing_app_list();
}

bool ShelfWidget::IsShowingContextMenu() const {
  return shelf_view_->IsShowingMenu();
}

bool ShelfWidget::IsShowingOverflowBubble() const {
  return shelf_view_->IsShowingOverflowBubble();
}

void ShelfWidget::SetFocusCycler(FocusCycler* focus_cycler) {
  delegate_view_->set_focus_cycler(focus_cycler);
  if (focus_cycler)
    focus_cycler->AddWidget(this);
}

FocusCycler* ShelfWidget::GetFocusCycler() {
  return delegate_view_->focus_cycler();
}

void ShelfWidget::Shutdown() {
  // Shutting down the status area widget may cause some widgets (e.g. bubbles)
  // to close, so uninstall the ShelfLayoutManager event filters first. Don't
  // reset the pointer until later because other widgets (e.g. app list) may
  // access it later in shutdown.
  if (shelf_layout_manager_)
    shelf_layout_manager_->PrepareForShutdown();

  if (status_area_widget_) {
    background_animator_.RemoveObserver(status_area_widget_);
    Shell::Get()->focus_cycler()->RemoveWidget(status_area_widget_);
    status_area_widget_->Shutdown();
    status_area_widget_ = nullptr;
  }

  CloseNow();
}

void ShelfWidget::UpdateIconPositionForPanel(WmWindow* panel) {
  ShelfID id =
      ShelfID::Deserialize(panel->aura_window()->GetProperty(kShelfIDKey));
  if (id.IsNull())
    return;

  WmWindow* shelf_window = WmWindow::Get(this->GetNativeWindow());
  shelf_view_->UpdatePanelIconPosition(
      id, shelf_window->ConvertRectFromScreen(panel->GetBoundsInScreen())
              .CenterPoint());
}

gfx::Rect ShelfWidget::GetScreenBoundsOfItemIconForWindow(WmWindow* window) {
  ShelfID id =
      ShelfID::Deserialize(window->aura_window()->GetProperty(kShelfIDKey));
  if (id.IsNull())
    return gfx::Rect();

  gfx::Rect bounds(shelf_view_->GetIdealBoundsOfItemIcon(id));
  gfx::Point screen_origin;
  views::View::ConvertPointToScreen(shelf_view_, &screen_origin);
  return gfx::Rect(screen_origin.x() + bounds.x(),
                   screen_origin.y() + bounds.y(), bounds.width(),
                   bounds.height());
}

AppListButton* ShelfWidget::GetAppListButton() const {
  return shelf_view_->GetAppListButton();
}

app_list::ApplicationDragAndDropHost*
ShelfWidget::GetDragAndDropHostForAppList() {
  return shelf_view_;
}

void ShelfWidget::OnWidgetActivationChanged(views::Widget* widget,
                                            bool active) {
  activating_as_fallback_ = false;
  if (active)
    delegate_view_->SetPaneFocusAndFocusDefault();
  else
    delegate_view_->GetFocusManager()->ClearFocus();
}

void ShelfWidget::UpdateShelfItemBackground(SkColor color) {
  shelf_view_->UpdateShelfItemBackground(color);
}

void ShelfWidget::WillDeleteShelfLayoutManager() {
  shelf_layout_manager_->RemoveObserver(this);
  shelf_layout_manager_ = nullptr;
}

}  // namespace ash
