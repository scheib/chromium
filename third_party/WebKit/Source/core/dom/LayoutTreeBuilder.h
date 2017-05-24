/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef LayoutTreeBuilder_h
#define LayoutTreeBuilder_h

#include "core/dom/Document.h"
#include "core/dom/LayoutTreeBuilderTraversal.h"
#include "core/dom/Node.h"
#include "core/dom/Text.h"
#include "core/layout/LayoutObject.h"
#include "platform/wtf/RefPtr.h"

namespace blink {

class ComputedStyle;

// The LayoutTreeBuilder class uses the DOM tree and CSS style rules as input to
// form a LayoutObject Tree which is then used for layout computations in a
// later stage.

// To construct the LayoutObject tree, the LayoutTreeBuilder does the following:
// 1. Starting at the root of the DOM tree, traverse each visible node.
//    Visibility is determined by
//    LayoutTreeBuilderFor{Element,Text}::ShouldCreateLayoutObject() functions.
// 2. For each visible node, ensure that the style has been resolved (either by
//    getting the ComputedStyle passed on to the LayoutTreeBuilder or by forcing
//    style resolution). This is done in LayoutTreeBuilderForElement::Style().
// 3. Emit visible LayoutObjects with content and their computed styles.
//    This is dealt with by the
//    LayoutTreeBuilderFor{Element,Text}::CreateLayoutObject() functions.
template <typename NodeType>
class LayoutTreeBuilder {
  STACK_ALLOCATED();

 protected:
  LayoutTreeBuilder(NodeType& node, LayoutObject* layout_object_parent)
      : node_(node), layout_object_parent_(layout_object_parent) {
    DCHECK(!node.GetLayoutObject());
    DCHECK(node.NeedsAttach());
    DCHECK(node.GetDocument().InStyleRecalc());
    DCHECK(node.InActiveDocument());
  }

  LayoutObject* NextLayoutObject() const {
    DCHECK(layout_object_parent_);

    // Avoid an O(N^2) walk over the children when reattaching all children of a
    // node.
    if (layout_object_parent_->GetNode() &&
        layout_object_parent_->GetNode()->NeedsAttach())
      return 0;

    return LayoutTreeBuilderTraversal::NextSiblingLayoutObject(*node_);
  }

  Member<NodeType> node_;
  LayoutObject* layout_object_parent_;
};

class LayoutTreeBuilderForElement : public LayoutTreeBuilder<Element> {
 public:
  LayoutTreeBuilderForElement(Element&, ComputedStyle*);

  void CreateLayoutObjectIfNeeded() {
    if (ShouldCreateLayoutObject())
      CreateLayoutObject();
  }

  ComputedStyle* ResolvedStyle() const { return style_.Get(); }

 private:
  LayoutObject* ParentLayoutObject() const;
  LayoutObject* NextLayoutObject() const;
  bool ShouldCreateLayoutObject() const;
  ComputedStyle& Style() const;
  void CreateLayoutObject();

  mutable RefPtr<ComputedStyle> style_;
};

class LayoutTreeBuilderForText : public LayoutTreeBuilder<Text> {
 public:
  LayoutTreeBuilderForText(Text& text,
                           LayoutObject* layout_parent,
                           ComputedStyle* style_from_parent)
      : LayoutTreeBuilder(text, layout_parent), style_(style_from_parent) {}

  RefPtr<ComputedStyle> style_;
  void CreateLayoutObject();
};

}  // namespace blink

#endif
