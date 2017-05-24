// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DEVTOOLS_UI_ELEMENT_H_
#define ASH_DEVTOOLS_UI_ELEMENT_H_

#include <vector>

#include "ash/ash_export.h"
#include "base/macros.h"
#include "ui/aura/window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace ash {
namespace devtools {

class UIElementDelegate;

// UIElement type.
enum UIElementType { WINDOW, WIDGET, VIEW };

class ASH_EXPORT UIElement {
 public:
  virtual ~UIElement();
  int node_id() const { return node_id_; };
  std::string GetTypeName() const;
  UIElement* parent() const { return parent_; };
  UIElementDelegate* delegate() const { return delegate_; };
  UIElementType type() const { return type_; };
  const std::vector<UIElement*>& children() const { return children_; };

  // |child| is inserted in front of |before|. If |before| is null, it
  // is inserted at the end. Parent takes ownership of the added child.
  void AddChild(UIElement* child, UIElement* before = nullptr);

  // Remove |child| out of vector |children_| but |child| is not destroyed.
  // The caller is responsible for destroying |child|.
  void RemoveChild(UIElement* child);

  // Move |child| to position new_index in |children_|.
  void ReorderChild(UIElement* child, int new_index);

  virtual void GetBounds(gfx::Rect* bounds) const = 0;
  virtual void SetBounds(const gfx::Rect& bounds) = 0;
  virtual void GetVisible(bool* visible) const = 0;
  virtual void SetVisible(bool visible) = 0;

  // If element exists, return its associated native window and its bounds.
  // Otherwise, return null and empty bounds.
  virtual std::pair<aura::Window*, gfx::Rect> GetNodeWindowAndBounds()
      const = 0;

  template <typename BackingT, typename T>
  static BackingT* GetBackingElement(UIElement* element) {
    return T::From(element);
  };

 protected:
  UIElement(const UIElementType type,
            UIElementDelegate* delegate,
            UIElement* parent);

 private:
  const int node_id_;
  const UIElementType type_;
  std::vector<UIElement*> children_;
  UIElement* parent_;
  UIElementDelegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(UIElement);
};

}  // namespace devtools
}  // namespace ash

#endif  // ASH_DEVTOOLS_UI_ELEMENT_H_
