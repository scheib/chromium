// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PaintInvalidator_h
#define PaintInvalidator_h

#include "core/layout/LayoutObject.h"
#include "core/paint/PaintPropertyTreeBuilder.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/wtf/Vector.h"

namespace blink {

struct PaintInvalidatorContext {
  USING_FAST_MALLOC(PaintInvalidatorContext);

 public:
  PaintInvalidatorContext() : parent_context(nullptr) {}

  PaintInvalidatorContext(const PaintInvalidatorContext& parent_context)
      : parent_context(&parent_context),
        subtree_flags(parent_context.subtree_flags),
        paint_invalidation_container(
            parent_context.paint_invalidation_container),
        paint_invalidation_container_for_stacked_contents(
            parent_context.paint_invalidation_container_for_stacked_contents),
        painting_layer(parent_context.painting_layer) {}

  // This method is virtual temporarily to adapt PaintInvalidatorContext and the
  // legacy PaintInvalidationState for code shared by old code and new code.
  virtual void MapLocalRectToVisualRectInBacking(const LayoutObject&,
                                                 LayoutRect&) const;

  bool NeedsVisualRectUpdate(const LayoutObject& object) const {
    if (!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
      return true;
#if DCHECK_IS_ON()
    if (force_visual_rect_update_for_checking_)
      return true;
#endif
    return object.NeedsPaintOffsetAndVisualRectUpdate() ||
           (subtree_flags & PaintInvalidatorContext::kSubtreeVisualRectUpdate);
  }

  const PaintInvalidatorContext* parent_context;

  enum SubtreeFlag {
    kSubtreeInvalidationChecking = 1 << 0,
    kSubtreeVisualRectUpdate = 1 << 1,
    kSubtreeFullInvalidation = 1 << 2,
    kSubtreeFullInvalidationForStackedContents = 1 << 3,
    kSubtreeSVGResourceChange = 1 << 4,

    // TODO(crbug.com/637313): This is temporary before we support filters in
    // paint property tree.
    kSubtreeSlowPathRect = 1 << 5,

    // When this flag is set, no paint or raster invalidation will be issued
    // for the subtree.
    //
    // Context: some objects in this paint walk, for example SVG resource
    // container subtrees, always paint onto temporary PaintControllers which
    // don't have cache, and don't actually have any raster regions, so they
    // don't need any invalidation. They are used as "painting subroutines"
    // for one or more other locations in SVG.
    kSubtreeNoInvalidation = 1 << 6,
  };
  unsigned subtree_flags = 0;

  // The following fields can be null only before
  // PaintInvalidator::updateContext().

  // The current paint invalidation container for normal flow objects.
  // It is the enclosing composited object.
  const LayoutBoxModelObject* paint_invalidation_container = nullptr;

  // The current paint invalidation container for stacked contents (stacking
  // contexts or positioned objects).  It is the nearest ancestor composited
  // object which establishes a stacking context.  See
  // Source/core/paint/README.md ### PaintInvalidationState for details on how
  // stacked contents' paint invalidation containers differ.
  const LayoutBoxModelObject*
      paint_invalidation_container_for_stacked_contents = nullptr;

  PaintLayer* painting_layer = nullptr;

  // Store the old visual rect in the paint invalidation backing's coordinates.
  // It does *not* account for composited scrolling.
  // See LayoutObject::adjustVisualRectForCompositedScrolling().
  LayoutRect old_visual_rect;
  // Use LayoutObject::visualRect() to get the new visual rect.

  // Store the origin of the object's local coordinates in the paint
  // invalidation backing's coordinates. They are used to detect layoutObject
  // shifts that force a full invalidation and invalidation check in subtree.
  // The points do *not* account for composited scrolling. See
  // LayoutObject::adjustVisualRectForCompositedScrolling().
  LayoutPoint old_location;
  LayoutPoint new_location;

 private:
  friend class PaintInvalidator;
  const PaintPropertyTreeBuilderFragmentContext* tree_builder_context_ =
      nullptr;

#if DCHECK_IS_ON()
  bool tree_builder_context_actually_needed_ = false;
  friend class FindVisualRectNeedingUpdateScope;
  friend class FindVisualRectNeedingUpdateScopeBase;
  mutable bool force_visual_rect_update_for_checking_ = false;
#endif
};

class PaintInvalidator {
 public:
  void InvalidatePaint(FrameView&,
                       const PaintPropertyTreeBuilderContext*,
                       PaintInvalidatorContext&);
  void InvalidatePaint(const LayoutObject&,
                       const PaintPropertyTreeBuilderContext*,
                       PaintInvalidatorContext&);

  // Process objects needing paint invalidation on the next frame.
  // See the definition of PaintInvalidationDelayedFull for more details.
  void ProcessPendingDelayedPaintInvalidations();

 private:
  friend struct PaintInvalidatorContext;
  template <typename Rect, typename Point>
  static LayoutRect MapLocalRectToVisualRectInBacking(
      const LayoutObject&,
      const Rect&,
      const PaintInvalidatorContext&);

  ALWAYS_INLINE LayoutRect
  ComputeVisualRectInBacking(const LayoutObject&,
                             const PaintInvalidatorContext&);
  ALWAYS_INLINE LayoutPoint
  ComputeLocationInBacking(const LayoutObject&, const PaintInvalidatorContext&);
  ALWAYS_INLINE void UpdatePaintingLayer(const LayoutObject&,
                                         PaintInvalidatorContext&);
  ALWAYS_INLINE void UpdatePaintInvalidationContainer(const LayoutObject&,
                                                      PaintInvalidatorContext&);
  ALWAYS_INLINE void UpdateVisualRectIfNeeded(
      const LayoutObject&,
      const PaintPropertyTreeBuilderContext*,
      PaintInvalidatorContext&);
  ALWAYS_INLINE void UpdateVisualRect(const LayoutObject&,
                                      PaintInvalidatorContext&);

  Vector<const LayoutObject*> pending_delayed_paint_invalidations_;
};

}  // namespace blink

#endif  // PaintInvalidator_h
