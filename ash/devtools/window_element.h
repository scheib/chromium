// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DEVTOOLS_WINDOW_ELEMENT_H_
#define ASH_DEVTOOLS_WINDOW_ELEMENT_H_

#include "ash/ash_export.h"
#include "ash/devtools/ui_element.h"
#include "base/macros.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/gfx/geometry/rect.h"

namespace ash {
namespace devtools {

class ASH_EXPORT WindowElement : public aura::WindowObserver, public UIElement {
 public:
  WindowElement(aura::Window* window,
                UIElementDelegate* ui_element_delegate,
                UIElement* parent);
  ~WindowElement() override;
  aura::Window* window() const { return window_; };

  // WindowObserver:
  void OnWindowHierarchyChanging(
      const aura::WindowObserver::HierarchyChangeParams& params) override;
  void OnWindowHierarchyChanged(
      const aura::WindowObserver::HierarchyChangeParams& params) override;
  void OnWindowStackingChanged(aura::Window* window) override;
  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override;

  // UIElement:
  void GetBounds(gfx::Rect* bounds) const override;
  void SetBounds(const gfx::Rect& bounds) override;
  void GetVisible(bool* visible) const override;
  void SetVisible(bool visible) override;
  std::pair<aura::Window*, gfx::Rect> GetNodeWindowAndBounds() const override;

  static aura::Window* From(UIElement* element);

 private:
  aura::Window* window_;

  DISALLOW_COPY_AND_ASSIGN(WindowElement);
};

}  // namespace devtools
}  // namespace ash

#endif  // ASH_DEVTOOLS_WINDOW_ELEMENT_H_
