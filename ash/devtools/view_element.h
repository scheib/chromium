// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DEVTOOLS_VIEW_ELEMENT_H_
#define ASH_DEVTOOLS_VIEW_ELEMENT_H_

#include "ash/ash_export.h"
#include "ash/devtools/ui_element.h"
#include "base/macros.h"
#include "ui/aura/window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

namespace ash {
namespace devtools {

class UIElementDelegate;

class ASH_EXPORT ViewElement : public views::ViewObserver, public UIElement {
 public:
  ViewElement(views::View* view,
              UIElementDelegate* ui_element_delegate,
              UIElement* parent);
  ~ViewElement() override;
  views::View* view() const { return view_; };

  // views::ViewObserver
  void OnChildViewRemoved(views::View* parent, views::View* view) override;
  void OnChildViewAdded(views::View* parent, views::View* view) override;
  void OnChildViewReordered(views::View* parent, views::View*) override;
  void OnViewBoundsChanged(views::View* view) override;

  // UIElement:
  void GetBounds(gfx::Rect* bounds) const override;
  void SetBounds(const gfx::Rect& bounds) override;
  void GetVisible(bool* visible) const override;
  void SetVisible(bool visible) override;
  std::pair<aura::Window*, gfx::Rect> GetNodeWindowAndBounds() const override;
  static views::View* From(UIElement* element);

 private:
  views::View* view_;

  DISALLOW_COPY_AND_ASSIGN(ViewElement);
};

}  // namespace devtools
}  // namespace ash

#endif  // ASH_DEVTOOLS_VIEW_ELEMENT_H_
