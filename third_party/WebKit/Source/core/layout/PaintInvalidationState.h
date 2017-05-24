// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PaintInvalidationState_h
#define PaintInvalidationState_h

#include "core/CoreExport.h"
#include "core/paint/PaintInvalidator.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/PaintInvalidationReason.h"
#include "platform/transforms/AffineTransform.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Noncopyable.h"

namespace blink {

class LayoutBoxModelObject;
class LayoutObject;
class LayoutView;
class PaintLayer;

// This is deprecated by PaintInvalidator when SlimmingPaintInvalidation is
// enabled.
//
// PaintInvalidationState is an optimization used during the paint
// invalidation phase.
//
// This class is extremely close to LayoutState so see the documentation
// of LayoutState for the class existence and performance benefits.
//
// The main difference with LayoutState is that it was customized for the
// needs of the paint invalidation systems (keeping visual rectangles
// instead of layout specific information).
//
// See Source/core/paint/README.md#Paint-invalidation for more details.

class CORE_EXPORT PaintInvalidationState {
  DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
  WTF_MAKE_NONCOPYABLE(PaintInvalidationState);

 public:
  PaintInvalidationState(const PaintInvalidationState& parent_state,
                         const LayoutObject&);

  // For root LayoutView, or when sub-frame LayoutView's
  // invalidateTreeIfNeeded() is called directly from
  // FrameView::invalidateTreeIfNeededRecursive() instead of the owner
  // LayoutPart.
  // TODO(wangxianzhu): Eliminate the latter case.
  PaintInvalidationState(
      const LayoutView&,
      Vector<const LayoutObject*>& pending_delayed_paint_invalidations);

  // When a PaintInvalidationState is constructed, it can be used to map
  // points/rects in the object's local space (border box space for
  // LayoutBoxes). After invalidation of the current object, before invalidation
  // of the subtrees, this method must be called to apply clip and scroll offset
  // etc. for creating child PaintInvalidationStates.
  void UpdateForChildren(PaintInvalidationReason);

  bool HasForcedSubtreeInvalidationFlags() const {
    return forced_subtree_invalidation_flags_;
  }

  bool ForcedSubtreeInvalidationCheckingWithinContainer() const {
    return forced_subtree_invalidation_flags_ &
           PaintInvalidatorContext::kSubtreeInvalidationChecking;
  }
  void SetForceSubtreeInvalidationCheckingWithinContainer() {
    forced_subtree_invalidation_flags_ |=
        PaintInvalidatorContext::kSubtreeInvalidationChecking;
  }

  bool ForcedSubtreeFullInvalidationWithinContainer() const {
    return forced_subtree_invalidation_flags_ &
           PaintInvalidatorContext::kSubtreeFullInvalidation;
  }

  bool ForcedSubtreeInvalidationRectUpdateWithinContainerOnly() const {
    return forced_subtree_invalidation_flags_ ==
           PaintInvalidatorContext::kSubtreeVisualRectUpdate;
  }
  void SetForceSubtreeInvalidationRectUpdateWithinContainer() {
    forced_subtree_invalidation_flags_ |=
        PaintInvalidatorContext::kSubtreeVisualRectUpdate;
  }

  const LayoutBoxModelObject& PaintInvalidationContainer() const {
    return *paint_invalidation_container_;
  }

  // Computes the location of the current object ((0,0) in the space of the
  // object) in the space of paint invalidation backing.
  LayoutPoint ComputeLocationInBacking(
      const LayoutPoint& visual_rect_location) const;

  // Returns the rect bounds needed to invalidate paint of this object,
  // in the space of paint invalidation backing.
  LayoutRect ComputeVisualRectInBacking() const;

  void MapLocalRectToVisualRectInBacking(LayoutRect&) const;

  PaintLayer& PaintingLayer() const;

  const LayoutObject& CurrentObject() const { return current_object_; }

 private:
  friend class VisualRectMappingTest;
  friend class PaintInvalidatorContextAdapter;

  inline PaintLayer& ChildPaintingLayer(const LayoutObject& child) const;

  void MapLocalRectToPaintInvalidationContainer(LayoutRect&) const;

  void UpdateForCurrentObject(const PaintInvalidationState& parent_state);
  void UpdateForNormalChildren();

  LayoutRect ComputeVisualRectInBackingForSVG() const;

  void AddClipRectRelativeToPaintOffset(const LayoutRect& local_clip_rect);

  const LayoutObject& current_object_;

  unsigned forced_subtree_invalidation_flags_;

  bool clipped_;
  bool clipped_for_absolute_position_;

  // Clip rect from paintInvalidationContainer if m_cachedOffsetsEnabled is
  // true.
  LayoutRect clip_rect_;
  LayoutRect clip_rect_for_absolute_position_;

  // x/y offset from the paintInvalidationContainer if m_cachedOffsetsEnabled is
  // true.
  // It includes relative positioning and scroll offsets.
  LayoutSize paint_offset_;
  LayoutSize paint_offset_for_absolute_position_;

  // Whether m_paintOffset[XXX] and m_clipRect[XXX] are valid and can be used
  // to map a rect from space of the current object to space of
  // paintInvalidationContainer.
  bool cached_offsets_enabled_;
  bool cached_offsets_for_absolute_position_enabled_;

  // The following two fields are never null. Declare them as pointers because
  // we need some logic to initialize them in the body of the constructor.

  // The current paint invalidation container for normal flow objects.
  // It is the enclosing composited object.
  const LayoutBoxModelObject* paint_invalidation_container_;

  // The current paint invalidation container for stacked contents (stacking
  // contexts or positioned objects).  It is the nearest ancestor composited
  // object which establishes a stacking context.  See
  // Source/core/paint/README.md ### PaintInvalidationState for details on how
  // stacked contents' paint invalidation containers differ.
  const LayoutBoxModelObject*
      paint_invalidation_container_for_stacked_contents_;

  const LayoutObject& container_for_absolute_position_;

  // Transform from the initial viewport coordinate system of an outermost
  // SVG root to the userspace _before_ the relevant element. Combining this
  // with |m_paintOffset| yields the "final" offset.
  AffineTransform svg_transform_;

  // Records objects needing paint invalidation on the next frame. See the
  // definition of PaintInvalidationDelayedFull for more details.
  Vector<const LayoutObject*>& pending_delayed_paint_invalidations_;

  PaintLayer& painting_layer_;

#if DCHECK_IS_ON()
  bool did_update_for_children_ = false;
#endif
};

// This is temporary to adapt legacy PaintInvalidationState to
// PaintInvalidatorContext
class PaintInvalidatorContextAdapter : public PaintInvalidatorContext {
 public:
  PaintInvalidatorContextAdapter(const PaintInvalidationState&);
  void MapLocalRectToVisualRectInBacking(const LayoutObject&,
                                         LayoutRect&) const override;

 private:
  const PaintInvalidationState& paint_invalidation_state_;
};

}  // namespace blink

#endif  // PaintInvalidationState_h
