// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DEVTOOLS_WIDGET_ELEMENT_H_
#define ASH_DEVTOOLS_WIDGET_ELEMENT_H_

#include "ash/ash_export.h"
#include "ash/devtools/ui_element.h"
#include "base/macros.h"
#include "ui/aura/window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/widget/widget_removals_observer.h"

namespace ash {
namespace devtools {

class UIElementDelegate;

class ASH_EXPORT WidgetElement : public views::WidgetRemovalsObserver,
                                 public views::WidgetObserver,
                                 public UIElement {
 public:
  WidgetElement(views::Widget* widget,
                UIElementDelegate* ui_element_delegate,
                UIElement* parent);
  ~WidgetElement() override;
  views::Widget* widget() const { return widget_; };

  // views::WidgetRemovalsObserver:
  void OnWillRemoveView(views::Widget* widget, views::View* view) override;

  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // UIElement:
  void GetBounds(gfx::Rect* bounds) const override;
  void SetBounds(const gfx::Rect& bounds) override;
  void GetVisible(bool* visible) const override;
  void SetVisible(bool visible) override;
  std::pair<aura::Window*, gfx::Rect> GetNodeWindowAndBounds() const override;

  static views::Widget* From(UIElement* element);

 private:
  views::Widget* widget_;

  DISALLOW_COPY_AND_ASSIGN(WidgetElement);
};

}  // namespace devtools
}  // namespace ash

#endif  // ASH_DEVTOOLS_WIDGET_ELEMENT_H_
