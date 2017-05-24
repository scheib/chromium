// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGLayoutInputNode_h
#define NGLayoutInputNode_h

#include "core/CoreExport.h"
#include "core/style/ComputedStyle.h"
#include "platform/heap/Handle.h"

namespace blink {

class LayoutObject;
class NGBreakToken;
class NGConstraintSpace;
class NGLayoutResult;
struct MinMaxContentSize;

// Represents the input to a layout algorithm for a given node. The layout
// engine should use the style, node type to determine which type of layout
// algorithm to use to produce fragments for this node.
class CORE_EXPORT NGLayoutInputNode
    : public GarbageCollectedFinalized<NGLayoutInputNode> {
 public:
  enum NGLayoutInputNodeType { kLegacyBlock = 0, kLegacyInline = 1 };

  bool IsInline() const { return type_ == kLegacyInline; }

  bool IsBlock() const { return type_ == kLegacyBlock; }

  bool IsFloating() const { return IsBlock() && Style().IsFloating(); }

  bool IsOutOfFlowPositioned() const {
    return IsBlock() && Style().HasOutOfFlowPosition();
  }

  virtual ~NGLayoutInputNode(){};

  // Performs layout on this input node, will return the layout result.
  virtual RefPtr<NGLayoutResult> Layout(NGConstraintSpace*, NGBreakToken*) = 0;

  // Returns the next sibling.
  virtual NGLayoutInputNode* NextSibling() = 0;

  // Returns the LayoutObject which is associated with this node.
  virtual LayoutObject* GetLayoutObject() const = 0;

  virtual MinMaxContentSize ComputeMinMaxContentSize() = 0;

  virtual const ComputedStyle& Style() const = 0;

  virtual String ToString() const = 0;

  NGLayoutInputNodeType Type() const {
    return static_cast<NGLayoutInputNodeType>(type_);
  }

#ifndef NDEBUG
  void ShowNodeTree() const;
#endif

  DEFINE_INLINE_VIRTUAL_TRACE() {}

 protected:
  explicit NGLayoutInputNode(NGLayoutInputNodeType type) : type_(type) {}

 private:
  unsigned type_ : 1;
};

}  // namespace blink

#endif  // NGLayoutInputNode_h
