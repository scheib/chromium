/*
   Copyright (C) 1997 Martin Jones (mjones@kde.org)
             (C) 1998 Waldo Bastian (bastian@kde.org)
             (C) 1998, 1999 Torben Weis (weis@kde.org)
             (C) 1999 Lars Knoll (knoll@kde.org)
             (C) 1999 Antti Koivisto (koivisto@kde.org)
   Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights
   reserved.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef FrameView_h
#define FrameView_h

#include <memory>
#include "core/CoreExport.h"
#include "core/dom/DocumentLifecycle.h"
#include "core/frame/FrameOrPlugin.h"
#include "core/frame/FrameViewAutoSizeInfo.h"
#include "core/frame/LayoutSubtreeRootList.h"
#include "core/frame/RootFrameViewport.h"
#include "core/layout/MapCoordinatesFlags.h"
#include "core/layout/ScrollAnchor.h"
#include "core/paint/FirstMeaningfulPaintDetector.h"
#include "core/paint/ObjectPaintProperties.h"
#include "core/paint/PaintInvalidationCapableScrollableArea.h"
#include "core/paint/PaintPhase.h"
#include "core/paint/ScrollbarManager.h"
#include "platform/PlatformFrameView.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/animation/CompositorAnimationHost.h"
#include "platform/animation/CompositorAnimationTimeline.h"
#include "platform/geometry/IntRect.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/Color.h"
#include "platform/graphics/CompositorElementId.h"
#include "platform/graphics/GraphicsLayerClient.h"
#include "platform/scroll/ScrollTypes.h"
#include "platform/scroll/Scrollbar.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/HashSet.h"
#include "platform/wtf/ListHashSet.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/ShapeProperties.h"
#include "public/platform/WebDisplayMode.h"
#include "public/platform/WebRect.h"

namespace blink {

class AXObjectCache;
class Cursor;
class DocumentLifecycle;
class Element;
class ElementVisibilityObserver;
class Frame;
class FloatSize;
class IntRect;
class JSONArray;
class JSONObject;
class LayoutItem;
class LayoutViewItem;
class LayoutPart;
class LocalFrame;
class KURL;
class Node;
class LayoutAnalyzer;
class LayoutBox;
class LayoutEmbeddedObject;
class LayoutObject;
class LayoutReplaced;
class LayoutScrollbarPart;
class LayoutView;
class PaintArtifactCompositor;
class PaintController;
class PaintInvalidationState;
class Page;
class PluginView;
class PrintContext;
class ScrollingCoordinator;
class TracedValue;
class TransformState;
struct AnnotatedRegionValue;
struct CompositedSelection;

typedef unsigned long long DOMTimeStamp;

class CORE_EXPORT FrameView final
    : public GarbageCollectedFinalized<FrameView>,
      public PlatformFrameView,
      public FrameOrPlugin,
      public PaintInvalidationCapableScrollableArea {
  USING_GARBAGE_COLLECTED_MIXIN(FrameView);

  friend class PaintControllerPaintTestBase;
  friend class Internals;
  friend class LayoutPart;  // for invalidateTreeIfNeeded

 public:
  static FrameView* Create(LocalFrame&);
  static FrameView* Create(LocalFrame&, const IntSize& initial_size);

  ~FrameView() override;

  void Invalidate() { InvalidateRect(IntRect(0, 0, Width(), Height())); }
  void InvalidateRect(const IntRect&);
  void SetFrameRect(const IntRect&) override;
  const IntRect& FrameRect() const override { return frame_rect_; }
  int X() const { return frame_rect_.X(); }
  int Y() const { return frame_rect_.Y(); }
  int Width() const { return frame_rect_.Width(); }
  int Height() const { return frame_rect_.Height(); }
  IntSize Size() const { return frame_rect_.Size(); }
  IntPoint Location() const { return frame_rect_.Location(); }
  void Resize(int width, int height) {
    SetFrameRect(IntRect(frame_rect_.X(), frame_rect_.Y(), width, height));
  }
  void Resize(const IntSize& size) { SetFrameRect(IntRect(Location(), size)); }

  LocalFrame& GetFrame() const {
    DCHECK(frame_);
    return *frame_;
  }

  Page* GetPage() const;

  // TODO(pilgrim) replace all instances of layoutView() with layoutViewItem()
  // https://crbug.com/499321
  LayoutView* GetLayoutView() const;
  LayoutViewItem GetLayoutViewItem() const;

  // If false, prevents scrollbars on the viewport even if web content would
  // make them appear. Also prevents user-input scrolls (but not programmatic
  // scrolls).
  // This API is root-layer-scrolling-aware (affects root PLSA in RLS mode).
  void SetCanHaveScrollbars(bool);
  bool CanHaveScrollbars() const { return can_have_scrollbars_; }

  Scrollbar* CreateScrollbar(ScrollbarOrientation);

  void SetContentsSize(const IntSize&);

  void UpdateLayout();
  bool DidFirstLayout() const;
  void ScheduleRelayout();
  void ScheduleRelayoutOfSubtree(LayoutObject*);
  bool LayoutPending() const;
  bool IsInPerformLayout() const;

  void ClearLayoutSubtreeRoot(const LayoutObject&);
  void AddOrthogonalWritingModeRoot(LayoutBox&);
  void RemoveOrthogonalWritingModeRoot(LayoutBox&);
  bool HasOrthogonalWritingModeRoots() const;
  void LayoutOrthogonalWritingModeRoots();
  void ScheduleOrthogonalWritingModeRootsForLayout();
  int LayoutCount() const { return layout_count_; }

  void CountObjectsNeedingLayout(unsigned& needs_layout_objects,
                                 unsigned& total_objects,
                                 bool& is_partial);

  bool NeedsLayout() const;
  bool CheckDoesNotNeedLayout() const;
  void SetNeedsLayout();

  void SetNeedsUpdateGeometries() { needs_update_geometries_ = true; }

  // Methods for getting/setting the size Blink should use to layout the
  // contents.
  // NOTE: Scrollbar exclusion is based on the FrameView's scrollbars. To
  // exclude scrollbars on the root PaintLayer, use LayoutView::layoutSize.
  IntSize GetLayoutSize(IncludeScrollbarsInRect = kExcludeScrollbars) const;
  void SetLayoutSize(const IntSize&);

  // If this is set to false, the layout size will need to be explicitly set by
  // the owner.  E.g. WebViewImpl sets its mainFrame's layout size manually
  void SetLayoutSizeFixedToFrameSize(bool is_fixed) {
    layout_size_fixed_to_frame_size_ = is_fixed;
  }
  bool LayoutSizeFixedToFrameSize() { return layout_size_fixed_to_frame_size_; }

  void SetInitialViewportSize(const IntSize&);
  int InitialViewportWidth() const;
  int InitialViewportHeight() const;

  void UpdateAcceleratedCompositingSettings();

  void RecalcOverflowAfterStyleChange();

  bool IsEnclosedInCompositingLayer() const;

  void Dispose() override;
  void DetachScrollbars();
  void RecalculateCustomScrollbarStyle();
  void InvalidateAllCustomScrollbarsOnActiveChanged();

  void Clear();

  // True if the FrameView's base background color is completely opaque.
  bool HasOpaqueBackground() const;

  Color BaseBackgroundColor() const;
  void SetBaseBackgroundColor(const Color&);
  void UpdateBaseBackgroundColorRecursively(const Color&);

  void AdjustViewSize();
  void AdjustViewSizeAndLayout();

  // Scale used to convert incoming input events.
  float InputEventsScaleFactor() const;

  // Offset used to convert incoming input events while emulating device metics.
  IntSize InputEventsOffsetForEmulation() const;
  void SetInputEventsTransformForEmulation(const IntSize&, float);

  void DidChangeScrollOffset();
  void DidUpdateElasticOverscroll();

  void ViewportSizeChanged(bool width_changed, bool height_changed);
  void MarkViewportConstrainedObjectsForLayout(bool width_changed,
                                               bool height_changed);

  AtomicString MediaType() const;
  void SetMediaType(const AtomicString&);
  void AdjustMediaTypeForPrinting(bool printing);

  WebDisplayMode DisplayMode() { return display_mode_; }
  void SetDisplayMode(WebDisplayMode);

  DisplayShape GetDisplayShape() { return display_shape_; }
  void SetDisplayShape(DisplayShape);

  // Fixed-position objects.
  typedef HashSet<LayoutObject*> ViewportConstrainedObjectSet;
  void AddViewportConstrainedObject(LayoutObject&);
  void RemoveViewportConstrainedObject(LayoutObject&);
  const ViewportConstrainedObjectSet* ViewportConstrainedObjects() const {
    return viewport_constrained_objects_.get();
  }
  bool HasViewportConstrainedObjects() const {
    return viewport_constrained_objects_ &&
           viewport_constrained_objects_->size() > 0;
  }

  // Objects with background-attachment:fixed.
  void AddBackgroundAttachmentFixedObject(LayoutObject*);
  void RemoveBackgroundAttachmentFixedObject(LayoutObject*);
  bool HasBackgroundAttachmentFixedObjects() const {
    return background_attachment_fixed_objects_.size();
  }
  bool HasBackgroundAttachmentFixedDescendants(const LayoutObject&) const;
  void InvalidateBackgroundAttachmentFixedObjects();

  void HandleLoadCompleted();

  void UpdateDocumentAnnotatedRegions() const;

  void DidAttachDocument();

  void RestoreScrollbar();

  void PostLayoutTimerFired(TimerBase*);

  bool SafeToPropagateScrollToParent() const {
    return safe_to_propagate_scroll_to_parent_;
  }
  void SetSafeToPropagateScrollToParent(bool is_safe) {
    safe_to_propagate_scroll_to_parent_ = is_safe;
  }

  void AddPart(LayoutPart*);
  void RemovePart(LayoutPart*);

  void UpdateGeometries();

  void AddPartToUpdate(LayoutEmbeddedObject&);

  Color DocumentBackgroundColor() const;

  // Run all needed lifecycle stages. After calling this method, all frames will
  // be in the lifecycle state PaintInvalidationClean.  If lifecycle throttling
  // is allowed (see DocumentLifecycle::AllowThrottlingScope), some frames may
  // skip the lifecycle update (e.g., based on visibility) and will not end up
  // being PaintInvalidationClean.
  void UpdateAllLifecyclePhases();

  // Everything except paint (the last phase).
  void UpdateAllLifecyclePhasesExceptPaint();

  // Computes the style, layout and compositing lifecycle stages if needed.
  // After calling this method, all frames will be in a lifecycle
  // state >= CompositingClean, and scrolling has been updated (unless
  // throttling is allowed).
  void UpdateLifecycleToCompositingCleanPlusScrolling();

  // Computes the style, layout, and compositing inputs lifecycle stages if
  // needed. After calling this method, all frames will be in a lifecycle state
  // >= CompositingInputsClean (unless throttling is allowed).
  void UpdateLifecycleToCompositingInputsClean();

  // Computes only the style and layout lifecycle stages.
  // After calling this method, all frames will be in a lifecycle
  // state >= LayoutClean (unless throttling is allowed).
  void UpdateLifecycleToLayoutClean();

  void ScheduleVisualUpdateForPaintInvalidationIfNeeded();

  bool InvalidateViewportConstrainedObjects();

  void IncrementLayoutObjectCount() { layout_object_counter_.Increment(); }
  void IncrementVisuallyNonEmptyCharacterCount(unsigned);
  void IncrementVisuallyNonEmptyPixelCount(const IntSize&);
  bool IsVisuallyNonEmpty() const { return is_visually_non_empty_; }
  void SetIsVisuallyNonEmpty() { is_visually_non_empty_ = true; }
  void EnableAutoSizeMode(const IntSize& min_size, const IntSize& max_size);
  void DisableAutoSizeMode();

  void ForceLayoutForPagination(const FloatSize& page_size,
                                const FloatSize& original_page_size,
                                float maximum_shrink_factor);

  enum UrlFragmentBehavior { kUrlFragmentScroll, kUrlFragmentDontScroll };
  // Updates the fragment anchor element based on URL's fragment identifier.
  // Updates corresponding ':target' CSS pseudo class on the anchor element.
  // If |UrlFragmentScroll| is passed in then makes the anchor element
  // focused and also visible by scrolling to it. The scroll offset is
  // maintained during the frame loading process.
  void ProcessUrlFragment(const KURL&,
                          UrlFragmentBehavior = kUrlFragmentScroll);
  void ClearFragmentAnchor();

  // Methods to convert points and rects between the coordinate space of the
  // layoutItem, and this view.
  IntRect ConvertFromLayoutItem(const LayoutItem&, const IntRect&) const;
  IntRect ConvertToLayoutItem(const LayoutItem&, const IntRect&) const;
  IntPoint ConvertFromLayoutItem(const LayoutItem&, const IntPoint&) const;
  IntPoint ConvertToLayoutItem(const LayoutItem&, const IntPoint&) const;

  bool IsFrameViewScrollCorner(LayoutScrollbarPart* scroll_corner) const {
    return scroll_corner_ == scroll_corner;
  }

  enum ScrollingReasons {
    kScrollable,
    kNotScrollableNoOverflow,
    kNotScrollableNotVisible,
    kNotScrollableExplicitlyDisabled
  };

  ScrollingReasons GetScrollingReasons() const;
  bool IsScrollable() const override;
  bool IsProgrammaticallyScrollable() override;

  IntPoint LastKnownMousePosition() const override;
  bool ShouldSetCursor() const;

  void SetCursor(const Cursor&);

  bool ScrollbarsCanBeActive() const override;
  void ScrollbarVisibilityChanged() override;
  void ScrollbarFrameRectChanged() override;

  // FIXME: Remove this method once plugin loading is decoupled from layout.
  void FlushAnyPendingPostLayoutTasks();

  bool ShouldSuspendScrollAnimations() const override;
  void ScrollbarStyleChanged() override;

  LayoutReplaced* EmbeddedReplacedContent() const;

  static void SetInitialTracksPaintInvalidationsForTesting(bool);

  void SetIsStoringCompositedLayerDebugInfo(bool val) {
    is_storing_composited_layer_debug_info_ = val;
  }

  // These methods are for testing.
  void SetTracksPaintInvalidations(bool);
  bool IsTrackingPaintInvalidations() const {
    return tracked_object_paint_invalidations_.get();
  }
  void TrackObjectPaintInvalidation(const DisplayItemClient&,
                                    PaintInvalidationReason);
  std::unique_ptr<JSONArray> TrackedObjectPaintInvalidationsAsJSON() const;

  using ScrollableAreaSet = HeapHashSet<Member<ScrollableArea>>;
  void AddScrollableArea(ScrollableArea*);
  void RemoveScrollableArea(ScrollableArea*);
  const ScrollableAreaSet* ScrollableAreas() const {
    return scrollable_areas_.Get();
  }

  void AddAnimatingScrollableArea(ScrollableArea*);
  void RemoveAnimatingScrollableArea(ScrollableArea*);
  const ScrollableAreaSet* AnimatingScrollableAreas() const {
    return animating_scrollable_areas_.Get();
  }

  // With CSS style "resize:" enabled, a little resizer handle will appear at
  // the bottom right of the object. We keep track of these resizer areas for
  // checking if touches (implemented using Scroll gesture) are targeting the
  // resizer.
  typedef HashSet<LayoutBox*> ResizerAreaSet;
  void AddResizerArea(LayoutBox&);
  void RemoveResizerArea(LayoutBox&);
  const ResizerAreaSet* ResizerAreas() const { return resizer_areas_.get(); }

  bool ShouldUseIntegerScrollOffset() const override;

  bool IsActive() const override;

  // Override scrollbar notifications to update the AXObject cache.
  void DidAddScrollbar(Scrollbar&, ScrollbarOrientation) override;

  // FIXME: This should probably be renamed as the 'inSubtreeLayout' parameter
  // passed around the FrameView layout methods can be true while this returns
  // false.
  bool IsSubtreeLayout() const { return !layout_subtree_root_list_.IsEmpty(); }

  // Sets the tickmarks for the FrameView, overriding the default behavior
  // which is to display the tickmarks corresponding to find results.
  // If |m_tickmarks| is empty, the default behavior is restored.
  void SetTickmarks(const Vector<IntRect>& tickmarks) {
    tickmarks_ = tickmarks;
    InvalidatePaintForTickmarks();
  }

  void InvalidatePaintForTickmarks();

  IntSize MaximumScrollOffsetInt() const override;

  // ScrollableArea interface
  void GetTickmarks(Vector<IntRect>&) const override;
  IntRect ScrollableAreaBoundingBox() const override;
  bool ScrollAnimatorEnabled() const override;
  bool UsesCompositedScrolling() const override;
  bool ShouldScrollOnMainThread() const override;
  PaintLayer* Layer() const override;
  GraphicsLayer* LayerForScrolling() const override;
  GraphicsLayer* LayerForHorizontalScrollbar() const override;
  GraphicsLayer* LayerForVerticalScrollbar() const override;
  GraphicsLayer* LayerForScrollCorner() const override;
  int ScrollSize(ScrollbarOrientation) const override;
  bool IsScrollCornerVisible() const override;
  bool UpdateAfterCompositingChange() override;
  bool UserInputScrollable(ScrollbarOrientation) const override;
  bool ShouldPlaceVerticalScrollbarOnLeft() const override;
  bool ScheduleAnimation() override;
  CompositorAnimationHost* GetCompositorAnimationHost() const override;
  CompositorAnimationTimeline* GetCompositorAnimationTimeline() const override;
  LayoutBox* GetLayoutBox() const override;
  FloatQuad LocalToVisibleContentQuad(const FloatQuad&,
                                      const LayoutObject*,
                                      unsigned = 0) const final;
  RefPtr<WebTaskRunner> GetTimerTaskRunner() const final;

  LayoutRect ScrollIntoView(const LayoutRect& rect_in_content,
                            const ScrollAlignment& align_x,
                            const ScrollAlignment& align_y,
                            ScrollType = kProgrammaticScroll) override;

  // The window that hosts the FrameView. The FrameView will communicate scrolls
  // and repaints to the host window in the window's coordinate space.
  PlatformChromeClient* GetChromeClient() const;

  // Functions for child manipulation and inspection.
  bool IsSelfVisible() const {
    return self_visible_;
  }  // Whether or not we have been explicitly marked as visible or not.
  bool IsParentVisible() const {
    return parent_visible_;
  }  // Whether or not our parent is visible.
  bool IsVisible() const {
    return self_visible_ && parent_visible_;
  }  // Whether or not we are actually visible.
  void SetParentVisible(bool) override;
  void SetSelfVisible(bool v) { self_visible_ = v; }
  void SetParent(FrameView*) override;
  FrameView* Parent() const override { return parent_; }
  void RemoveChild(FrameOrPlugin*);
  using PluginSet = HeapHashSet<Member<PluginView>>;
  const PluginSet& Plugins() const { return plugins_; }
  void AddPlugin(PluginView*);
  // Custom scrollbars in PaintLayerScrollableArea need to be called with
  // StyleChanged whenever window focus is changed.
  void RemoveScrollbar(Scrollbar*);
  void AddScrollbar(Scrollbar*);

  // If the scroll view does not use a native widget, then it will have
  // cross-platform Scrollbars. These functions can be used to obtain those
  // scrollbars.
  Scrollbar* HorizontalScrollbar() const override {
    return scrollbar_manager_.HorizontalScrollbar();
  }
  Scrollbar* VerticalScrollbar() const override {
    return scrollbar_manager_.VerticalScrollbar();
  }
  LayoutScrollbarPart* ScrollCorner() const override { return scroll_corner_; }

  void PositionScrollbarLayers();

  // Functions for setting and retrieving the scrolling mode in each axis
  // (horizontal/vertical). The mode has values of AlwaysOff, AlwaysOn, and
  // Auto. AlwaysOff means never show a scrollbar, AlwaysOn means always show a
  // scrollbar.  Auto means show a scrollbar only when one is needed.
  // Note that for platforms with native widgets, these modes are considered
  // advisory. In other words the underlying native widget may choose not to
  // honor the requested modes.
  void SetScrollbarModes(ScrollbarMode horizontal_mode,
                         ScrollbarMode vertical_mode,
                         bool horizontal_lock = false,
                         bool vertical_lock = false);
  void SetHorizontalScrollbarMode(ScrollbarMode mode, bool lock = false) {
    SetScrollbarModes(mode, VerticalScrollbarMode(), lock,
                      VerticalScrollbarLock());
  }
  void SetVerticalScrollbarMode(ScrollbarMode mode, bool lock = false) {
    SetScrollbarModes(HorizontalScrollbarMode(), mode,
                      HorizontalScrollbarLock(), lock);
  }
  ScrollbarMode HorizontalScrollbarMode() const {
    return horizontal_scrollbar_mode_;
  }
  ScrollbarMode VerticalScrollbarMode() const {
    return vertical_scrollbar_mode_;
  }

  void SetHorizontalScrollbarLock(bool lock = true) {
    horizontal_scrollbar_lock_ = lock;
  }
  bool HorizontalScrollbarLock() const { return horizontal_scrollbar_lock_; }
  void SetVerticalScrollbarLock(bool lock = true) {
    vertical_scrollbar_lock_ = lock;
  }
  bool VerticalScrollbarLock() const { return vertical_scrollbar_lock_; }

  void SetScrollingModesLock(bool lock = true) {
    horizontal_scrollbar_lock_ = vertical_scrollbar_lock_ = lock;
  }

  // The visible content rect has a location that is the scrolled offset of
  // the document. The width and height are the layout viewport width and
  // height. By default the scrollbars themselves are excluded from this
  // rectangle, but an optional boolean argument allows them to be included.
  IntRect VisibleContentRect(
      IncludeScrollbarsInRect = kExcludeScrollbars) const override;
  IntSize VisibleContentSize(
      IncludeScrollbarsInRect = kExcludeScrollbars) const;

  // Clips the provided rect to the visible content area. For this purpose, we
  // also query the chrome client for any active overrides to the visible area
  // (e.g. DevTool's viewport override).
  void ClipPaintRect(FloatRect*) const;

  // Functions for getting/setting the size of the document contained inside the
  // FrameView (as an IntSize or as individual width and height values).
  // Always at least as big as the visibleWidth()/visibleHeight().
  IntSize ContentsSize() const override;
  int ContentsWidth() const { return ContentsSize().Width(); }
  int ContentsHeight() const { return ContentsSize().Height(); }

  // Functions for querying the current scrolled offset (both as a point, a
  // size, or as individual X and Y values).  Be careful in using the Float
  // version getScrollOffset() and getScrollOffset(). They are meant to be used
  // to communicate the fractional scroll offset with chromium compositor which
  // can do sub-pixel positioning.  Do not call these if the scroll offset is
  // used in Blink for positioning. Use the Int version instead.
  IntSize ScrollOffsetInt() const override {
    return ToIntSize(VisibleContentRect().Location());
  }
  ScrollOffset GetScrollOffset() const override { return scroll_offset_; }
  ScrollOffset PendingScrollDelta() const { return pending_scroll_delta_; }
  IntSize MinimumScrollOffsetInt()
      const override;  // The minimum offset we can be scrolled to.
  int ScrollX() const { return ScrollOffsetInt().Width(); }
  int ScrollY() const { return ScrollOffsetInt().Height(); }

  // Scroll the actual contents of the view (either blitting or invalidating as
  // needed).
  void ScrollContents(const IntSize& scroll_delta);

  // This gives us a means of blocking updating our scrollbars until the first
  // layout has occurred.
  void SetScrollbarsSuppressed(bool suppressed) {
    scrollbars_suppressed_ = suppressed;
  }
  bool ScrollbarsSuppressed() const { return scrollbars_suppressed_; }

  // Methods for converting between this frame and other coordinate spaces.
  // For definitions and an explanation of the varous spaces, please see:
  // http://www.chromium.org/developers/design-documents/blink-coordinate-spaces
  IntPoint RootFrameToContents(const IntPoint&) const;
  FloatPoint RootFrameToContents(const FloatPoint&) const;
  IntRect RootFrameToContents(const IntRect&) const;
  IntPoint ContentsToRootFrame(const IntPoint&) const;
  IntRect ContentsToRootFrame(const IntRect&) const;

  IntRect ViewportToContents(const IntRect&) const;
  IntRect ContentsToViewport(const IntRect&) const;
  IntPoint ContentsToViewport(const IntPoint&) const;
  IntPoint ViewportToContents(const IntPoint&) const;

  // FIXME: Some external callers expect to get back a rect that's positioned
  // in viewport space, but sized in CSS pixels. This is an artifact of the
  // old pinch-zoom path. These callers should be converted to expect a rect
  // fully in viewport space. crbug.com/459591.
  IntPoint SoonToBeRemovedUnscaledViewportToContents(const IntPoint&) const;

  // Methods for converting between Frame and Content (i.e. Document)
  // coordinates.  Frame coordinates are relative to the top left corner of the
  // frame and so they are affected by scroll offset. Content coordinates are
  // relative to the document's top left corner and thus are not affected by
  // scroll offset.
  IntPoint ContentsToFrame(const IntPoint&) const;
  IntRect ContentsToFrame(const IntRect&) const;
  IntPoint FrameToContents(const IntPoint&) const;
  FloatPoint FrameToContents(const FloatPoint&) const;
  IntRect FrameToContents(const IntRect&) const;

  // Functions for converting to screen coordinates.
  IntRect ContentsToScreen(const IntRect&) const;

  // For platforms that need to hit test scrollbars from within the engine's
  // event handlers (like Win32).
  Scrollbar* ScrollbarAtFramePoint(const IntPoint&);

  IntRect ConvertToRootFrame(const IntRect&) const;
  IntPoint ConvertToRootFrame(const IntPoint&) const;
  IntRect ConvertFromRootFrame(const IntRect&) const;
  IntPoint ConvertFromRootFrame(const IntPoint&) const override;
  FloatPoint ConvertFromRootFrame(const FloatPoint&) const;
  IntPoint ConvertSelfToChild(const FrameOrPlugin&, const IntPoint&) const;

  // Handles painting of the contents of the view as well as the scrollbars.
  void Paint(GraphicsContext&, const CullRect&) const override;
  void Paint(GraphicsContext&, const GlobalPaintFlags, const CullRect&) const;
  void PaintContents(GraphicsContext&,
                     const GlobalPaintFlags,
                     const IntRect& damage_rect) const;

  void Show() override;
  void Hide() override;

  bool IsPointInScrollbarCorner(const IntPoint&);
  bool ScrollbarCornerPresent() const;
  IntRect ScrollCornerRect() const override;

  IntPoint ConvertFromContainingFrameViewBaseToScrollbar(
      const Scrollbar&,
      const IntPoint&) const override;

  bool IsFrameView() const override { return true; }

  DECLARE_VIRTUAL_TRACE();
  void NotifyPageThatContentAreaWillPaint() const;
  FrameView* ParentFrameView() const;

  // Returns the scrollable area for the frame. For the root frame, this will
  // be the RootFrameViewport, which adds pinch-zoom semantics to scrolling.
  // For non-root frames, this will be the the ScrollableArea used by the
  // FrameView, depending on whether root-layer-scrolls is enabled.
  ScrollableArea* GetScrollableArea();

  // Used to get at the underlying layoutViewport in the rare instances where
  // we actually want to scroll *just* the layout viewport (e.g. when sending
  // deltas from CC). For typical scrolling cases, use getScrollableArea().
  ScrollableArea* LayoutViewportScrollableArea();

  // If this is the main frame, this will return the RootFrameViewport used
  // to scroll the main frame. Otherwise returns nullptr. Unless you need a
  // unique method on RootFrameViewport, you should probably use
  // getScrollableArea.
  RootFrameViewport* GetRootFrameViewport();

  int ViewportWidth() const;

  LayoutAnalyzer* GetLayoutAnalyzer() { return analyzer_.get(); }

  // Returns true if this frame should not render or schedule visual updates.
  bool ShouldThrottleRendering() const;

  // Returns true if this frame could potentially skip rendering and avoid
  // scheduling visual updates.
  bool CanThrottleRendering() const;
  bool IsHiddenForThrottling() const { return hidden_for_throttling_; }
  void SetupRenderThrottling();

  // For testing, run pending intersection observer notifications for this
  // frame.
  void UpdateRenderThrottlingStatusForTesting();

  void BeginLifecycleUpdates();

  // Paint properties for SPv2 Only.
  void SetPreTranslation(
      PassRefPtr<TransformPaintPropertyNode> pre_translation) {
    pre_translation_ = std::move(pre_translation);
  }
  TransformPaintPropertyNode* PreTranslation() const {
    return pre_translation_.Get();
  }

  void SetScrollTranslation(
      PassRefPtr<TransformPaintPropertyNode> scroll_translation) {
    scroll_translation_ = std::move(scroll_translation);
  }
  TransformPaintPropertyNode* ScrollTranslation() const {
    return scroll_translation_.Get();
  }

  void SetContentClip(PassRefPtr<ClipPaintPropertyNode> content_clip) {
    content_clip_ = std::move(content_clip);
  }
  ClipPaintPropertyNode* ContentClip() const { return content_clip_.Get(); }

  // The property tree state that should be used for painting contents. These
  // properties are either created by this FrameView or are inherited from
  // an ancestor.
  void SetTotalPropertyTreeStateForContents(
      std::unique_ptr<PropertyTreeState> state) {
    total_property_tree_state_for_contents_ = std::move(state);
  }
  const PropertyTreeState* TotalPropertyTreeStateForContents() const {
    return total_property_tree_state_for_contents_.get();
  }

  // Paint properties (e.g., m_preTranslation, etc.) are built from the
  // FrameView's state (e.g., x(), y(), etc.) as well as inherited context.
  // When these inputs change, setNeedsPaintPropertyUpdate will cause a paint
  // property tree update during the next document lifecycle update.
  // setNeedsPaintPropertyUpdate also sets the owning layout tree as needing a
  // paint property update.
  void SetNeedsPaintPropertyUpdate();
#if DCHECK_IS_ON()
  // Similar to setNeedsPaintPropertyUpdate() but does not set the owning layout
  // tree as needing a paint property update.
  void SetOnlyThisNeedsPaintPropertyUpdateForTesting() {
    needs_paint_property_update_ = true;
  }
#endif
  void ClearNeedsPaintPropertyUpdate() {
    DCHECK_EQ(Lifecycle().GetState(), DocumentLifecycle::kInPrePaint);
    needs_paint_property_update_ = false;
  }
  bool NeedsPaintPropertyUpdate() const { return needs_paint_property_update_; }

  // Set when the whole frame subtree needs full paint property update,
  // e.g. when beginning or finishing printing.
  void SetSubtreeNeedsPaintPropertyUpdate();

  // TODO(ojan): Merge this with IntersectionObserver once it lands.
  IntRect ComputeVisibleArea();

  // Viewport size that should be used for viewport units (i.e. 'vh'/'vw').
  // May include the size of browser controls. See implementation for further
  // documentation.
  FloatSize ViewportSizeForViewportUnits() const;

  // Initial containing block size for evaluating viewport-dependent media
  // queries.
  FloatSize ViewportSizeForMediaQueries() const;

  ScrollAnchor* GetScrollAnchor() override { return &scroll_anchor_; }
  void ClearScrollAnchor();
  bool ShouldPerformScrollAnchoring() const override;
  void EnqueueScrollAnchoringAdjustment(ScrollableArea*);
  void PerformScrollAnchoringAdjustments();

  // For PaintInvalidator temporarily. TODO(wangxianzhu): Move into
  // PaintInvalidator.
  void InvalidatePaint(const PaintInvalidationState&);

  // Only for SPv2.
  std::unique_ptr<JSONObject> CompositedLayersAsJSON(LayerTreeFlags);

  // Recursively update frame tree. Each frame has its only
  // scroll on main reason. Given the following frame tree
  //.. A...
  //../.\..
  //.B...C.
  //.|.....
  //.D.....
  // If B has fixed background-attachment but other frames
  // don't, both A and C should scroll on cc. Frame D should
  // scrolled on main thread as its ancestor B.
  void UpdateSubFrameScrollOnMainReason(const Frame&,
                                        MainThreadScrollingReasons);
  String MainThreadScrollingReasonsAsText() const;
  // Main thread scrolling reasons including reasons from ancestors.
  MainThreadScrollingReasons GetMainThreadScrollingReasons() const;
  // Main thread scrolling reasons for this object only. For all reasons,
  // see: mainThreadScrollingReasons().
  MainThreadScrollingReasons MainThreadScrollingReasonsPerFrame() const;

  bool HasVisibleSlowRepaintViewportConstrainedObjects() const;

  // Called on a view for a LocalFrame with a RemoteFrame parent. This makes
  // viewport intersection available that accounts for remote ancestor frames
  // and their respective scroll positions, clips, etc.
  void SetViewportIntersectionFromParent(const IntRect&);
  IntRect RemoteViewportIntersection();

  // This method uses localToAncestorQuad to map a rect into an ancestor's
  // coordinate space, while guaranteeing that the top-level scroll offset
  // is accounted for. This is needed because LayoutView::mapLocalToAncestor()
  // implicitly includes the ancestor frame's scroll offset when there is
  // a remote frame in the ancestor chain, but does not include it when
  // there are only local frames in the frame tree.
  void MapQuadToAncestorFrameIncludingScrollOffset(
      LayoutRect&,
      const LayoutObject* descendant,
      const LayoutView* ancestor,
      MapCoordinatesFlags mode);

  bool MapToVisualRectInTopFrameSpace(LayoutRect&);

  void ApplyTransformForTopFrameSpace(TransformState&);

  // TODO(kenrb): These are temporary methods pending resolution of
  // https://crbug.com/680606. Animation timelines and hosts for scrolling
  // are normally owned by ScrollingCoordinator, but there is only one
  // of those objects per page. To get around this, we temporarily stash a
  // unique timeline and host on each OOPIF FrameView.
  void SetAnimationTimeline(std::unique_ptr<CompositorAnimationTimeline>);
  void SetAnimationHost(std::unique_ptr<CompositorAnimationHost>);

  void CrossOriginStatusChanged();

  // The visual viewport can supply scrollbars which affect the existence of
  // our scrollbars (see: computeScrollbarExistence).
  void VisualViewportScrollbarsChanged();

  LayoutUnit CaretWidth() const;

 protected:
  // Scroll the content via the compositor.
  bool ScrollContentsFastPath(const IntSize& scroll_delta);

  // Scroll the content by invalidating everything.
  void ScrollContentsSlowPath();

  ScrollBehavior ScrollBehaviorStyle() const override;

  void ScrollContentsIfNeeded();

  enum ComputeScrollbarExistenceOption { kFirstPass, kIncremental };
  void ComputeScrollbarExistence(bool& new_has_horizontal_scrollbar,
                                 bool& new_has_vertical_scrollbar,
                                 const IntSize& doc_size,
                                 ComputeScrollbarExistenceOption = kFirstPass);
  void UpdateScrollbarGeometry();

  // Called to update the scrollbars to accurately reflect the state of the
  // view.
  void UpdateScrollbars();
  void UpdateScrollbarsIfNeeded();

  class InUpdateScrollbarsScope {
    STACK_ALLOCATED();

   public:
    explicit InUpdateScrollbarsScope(FrameView* view)
        : scope_(&view->in_update_scrollbars_, true) {}

   private:
    AutoReset<bool> scope_;
  };

  // Only for LayoutPart to traverse into sub frames during paint invalidation.
  void DeprecatedInvalidateTree(const PaintInvalidationState&);

 private:
  explicit FrameView(LocalFrame&, IntRect);
  class ScrollbarManager : public blink::ScrollbarManager {
    DISALLOW_NEW();

    // Helper class to manage the life cycle of Scrollbar objects.
   public:
    ScrollbarManager(FrameView& scroller) : blink::ScrollbarManager(scroller) {}

    void SetHasHorizontalScrollbar(bool has_scrollbar) override;
    void SetHasVerticalScrollbar(bool has_scrollbar) override;

    // TODO(ymalik): This should be hidden and all calls should go through
    // setHas*Scrollbar functions above.
    Scrollbar* CreateScrollbar(ScrollbarOrientation) override;

   protected:
    void DestroyScrollbar(ScrollbarOrientation) override;
  };

  void UpdateScrollOffset(const ScrollOffset&, ScrollType) override;

  void UpdateScrollbarEnabledState();

  void DispatchEventsForPrintingOnAllFrames();

  void SetupPrintContext();
  void ClearPrintContext();

  void UpdateLifecyclePhasesInternal(
      DocumentLifecycle::LifecycleState target_state);

  void DeprecatedInvalidateTreeRecursive();
  void ScrollContentsIfNeededRecursive();
  void UpdateStyleAndLayoutIfNeededRecursive();
  void PrePaint();
  void PaintTree();
  void PaintGraphicsLayerRecursively(GraphicsLayer*);

  void UpdateStyleAndLayoutIfNeededRecursiveInternal();
  void DeprecatedInvalidateTreeRecursiveInternal();

  void PushPaintArtifactToCompositor(
      CompositorElementIdSet& composited_element_ids);

  void Reset();
  void Init();

  void ClearLayoutSubtreeRootsAndMarkContainingBlocks();

  // Called when our frame rect changes (or the rect/scroll offset of an
  // ancestor changes).
  void FrameRectsChanged() override;

  bool ContentsInCompositedLayer() const;

  void UpdateCounters();
  void ForceLayoutParentViewIfNeeded();
  void PerformPreLayoutTasks();
  void PerformLayout(bool in_subtree_layout);
  void ScheduleOrPerformPostLayoutTasks();
  void PerformPostLayoutTasks();

  void RecordDeferredLoadingStats();

  DocumentLifecycle& Lifecycle() const;

  void ContentsResized() override;
  void ScrollbarExistenceDidChange();

  // Methods to do point conversion via layoutObjects, in order to take
  // transforms into account.
  IntRect ConvertToContainingFrameViewBase(const IntRect&) const;
  IntPoint ConvertToContainingFrameViewBase(const IntPoint&) const;
  IntRect ConvertFromContainingFrameViewBase(const IntRect&) const;
  IntPoint ConvertFromContainingFrameViewBase(const IntPoint&) const;

  void DidChangeGlobalRootScroller() override;

  void UpdateGeometriesIfNeeded();

  bool WasViewportResized();
  void SendResizeEventIfNeeded();

  void UpdateParentScrollableAreaSet();

  void ScheduleUpdatePluginsIfNecessary();
  void UpdatePluginsTimerFired(TimerBase*);
  bool UpdatePlugins();

  bool ProcessUrlFragmentHelper(const String&, UrlFragmentBehavior);
  void SetFragmentAnchor(Node*);
  void ScrollToFragmentAnchor();
  void DidScrollTimerFired(TimerBase*);

  void UpdateLayersAndCompositingAfterScrollIfNeeded();

  static bool ComputeCompositedSelection(LocalFrame&, CompositedSelection&);
  void UpdateCompositedSelectionIfNeeded();

  // Returns true if the FrameView's own scrollbars overlay its content when
  // visible.
  bool HasOverlayScrollbars() const;

  // Returns true if the frame should use custom scrollbars. If true, sets
  // customScrollbarElement to the element that supplies the scrollbar's style
  // information.
  bool ShouldUseCustomScrollbars(Element*& custom_scrollbar_element) const;

  // Returns true if a scrollbar needs to go from native -> custom or vice
  // versa, or if a custom scrollbar has a stale owner.
  bool NeedsScrollbarReconstruction() const;

  bool ShouldIgnoreOverflowHidden() const;

  void UpdateScrollCorner();

  AXObjectCache* AxObjectCache() const;

  void SetLayoutSizeInternal(const IntSize&);

  bool AdjustScrollbarExistence(ComputeScrollbarExistenceOption = kFirstPass);
  void AdjustScrollbarOpacity();
  void AdjustScrollOffsetFromUpdateScrollbars();
  bool VisualViewportSuppliesScrollbars();

  ScrollingCoordinator* GetScrollingCoordinator() const;

  void PrepareLayoutAnalyzer();
  std::unique_ptr<TracedValue> AnalyzerCounters();

  // LayoutObject for the viewport-defining element (see
  // Document::viewportDefiningElement).
  LayoutObject* ViewportLayoutObject() const;

  void CollectAnnotatedRegions(LayoutObject&,
                               Vector<AnnotatedRegionValue>&) const;

  template <typename Function>
  void ForAllChildViewsAndPlugins(const Function&);

  template <typename Function>
  void ForAllChildFrameViews(const Function&);

  template <typename Function>
  void ForAllNonThrottledFrameViews(const Function&);

  void UpdateViewportIntersectionsForSubtree(
      DocumentLifecycle::LifecycleState target_state);

  enum ForceThrottlingInvalidationBehavior {
    kDontForceThrottlingInvalidation,
    kForceThrottlingInvalidation
  };
  enum NotifyChildrenBehavior { kDontNotifyChildren, kNotifyChildren };
  void UpdateRenderThrottlingStatus(
      bool hidden,
      bool subtree_throttled,
      ForceThrottlingInvalidationBehavior = kDontForceThrottlingInvalidation,
      NotifyChildrenBehavior = kNotifyChildren);
  void NotifyResizeObservers();

  // PaintInvalidationCapableScrollableArea
  LayoutScrollbarPart* Resizer() const override { return nullptr; }

  bool CheckLayoutInvalidationIsAllowed() const;

  PaintController* GetPaintController() { return paint_controller_.get(); }

  LayoutSize size_;

  typedef HashSet<RefPtr<LayoutEmbeddedObject>> EmbeddedObjectSet;
  EmbeddedObjectSet part_update_set_;

  // FIXME: These are just "children" of the FrameView and should be
  // Member<FrameView> instead.
  HashSet<RefPtr<LayoutPart>> parts_;

  Member<LocalFrame> frame_;

  IntRect frame_rect_;
  Member<FrameView> parent_;
  bool self_visible_;
  bool parent_visible_;

  WebDisplayMode display_mode_;

  DisplayShape display_shape_;

  bool can_have_scrollbars_;

  bool has_pending_layout_;
  LayoutSubtreeRootList layout_subtree_root_list_;
  DepthOrderedLayoutObjectList orthogonal_writing_mode_root_list_;

  bool layout_scheduling_enabled_;
  bool in_synchronous_post_layout_;
  int layout_count_;
  unsigned nested_layout_count_;
  TaskRunnerTimer<FrameView> post_layout_tasks_timer_;
  TaskRunnerTimer<FrameView> update_plugins_timer_;

  bool first_layout_;
  Color base_background_color_;
  IntSize last_viewport_size_;
  float last_zoom_factor_;

  AtomicString media_type_;
  AtomicString media_type_when_not_printing_;

  bool safe_to_propagate_scroll_to_parent_;

  unsigned visually_non_empty_character_count_;
  uint64_t visually_non_empty_pixel_count_;
  bool is_visually_non_empty_;
  FirstMeaningfulPaintDetector::LayoutObjectCounter layout_object_counter_;

  Member<Node> fragment_anchor_;

  // layoutObject to hold our custom scroll corner.
  LayoutScrollbarPart* scroll_corner_;

  Member<ScrollableAreaSet> scrollable_areas_;
  Member<ScrollableAreaSet> animating_scrollable_areas_;
  std::unique_ptr<ResizerAreaSet> resizer_areas_;
  std::unique_ptr<ViewportConstrainedObjectSet> viewport_constrained_objects_;
  unsigned sticky_position_object_count_;
  ViewportConstrainedObjectSet background_attachment_fixed_objects_;
  Member<FrameViewAutoSizeInfo> auto_size_info_;

  IntSize input_events_offset_for_emulation_;
  float input_events_scale_factor_for_emulation_;

  IntSize layout_size_;
  IntSize initial_viewport_size_;
  bool layout_size_fixed_to_frame_size_;

  TaskRunnerTimer<FrameView> did_scroll_timer_;

  Vector<IntRect> tickmarks_;

  bool needs_update_geometries_;

#if DCHECK_IS_ON()
  // Verified when finalizing.
  bool has_been_disposed_ = false;
#endif

  ScrollbarMode horizontal_scrollbar_mode_;
  ScrollbarMode vertical_scrollbar_mode_;

  bool horizontal_scrollbar_lock_;
  bool vertical_scrollbar_lock_;

  PluginSet plugins_;
  HeapHashSet<Member<Scrollbar>> scrollbars_;

  ScrollOffset pending_scroll_delta_;
  ScrollOffset scroll_offset_;
  IntSize contents_size_;

  bool scrollbars_suppressed_;

  bool in_update_scrollbars_;

  std::unique_ptr<LayoutAnalyzer> analyzer_;

  // Mark if something has changed in the mapping from Frame to GraphicsLayer
  // and the Frame Timing regions should be recalculated.
  bool frame_timing_requests_dirty_;

  // Exists only on root frame.
  // TODO(bokan): crbug.com/484188. We should specialize FrameView for the
  // main frame.
  Member<RootFrameViewport> viewport_scrollable_area_;

  // The following members control rendering pipeline throttling for this
  // frame. They are only updated in response to intersection observer
  // notifications, i.e., not in the middle of the lifecycle.
  bool hidden_for_throttling_;
  bool subtree_throttled_;
  bool lifecycle_updates_throttled_;

  // Paint properties for SPv2 Only.
  // The hierarchy of transform subtree created by a FrameView.
  // [ preTranslation ]               The offset from FrameView::FrameRect.
  //     |                            Establishes viewport.
  //     +---[ scrollTranslation ]    Frame scrolling.
  // TODO(trchen): These will not be needed once settings->rootLayerScrolls() is
  // enabled.
  RefPtr<TransformPaintPropertyNode> pre_translation_;
  RefPtr<TransformPaintPropertyNode> scroll_translation_;
  // The content clip clips the document (= LayoutView) but not the scrollbars.
  // TODO(trchen): This will not be needed once settings->rootLayerScrolls() is
  // enabled.
  RefPtr<ClipPaintPropertyNode> content_clip_;
  // The property tree state that should be used for painting contents. These
  // properties are either created by this FrameView or are inherited from
  // an ancestor.
  std::unique_ptr<PropertyTreeState> total_property_tree_state_for_contents_;
  // Whether the paint properties need to be updated. For more details, see
  // FrameView::needsPaintPropertyUpdate().
  bool needs_paint_property_update_;

  // This is set on the local root frame view only.
  DocumentLifecycle::LifecycleState
      current_update_lifecycle_phases_target_state_;

  ScrollAnchor scroll_anchor_;
  using AnchoringAdjustmentQueue =
      HeapLinkedHashSet<WeakMember<ScrollableArea>>;
  AnchoringAdjustmentQueue anchoring_adjustment_queue_;

  // ScrollbarManager holds the Scrollbar instances.
  ScrollbarManager scrollbar_manager_;

  bool needs_scrollbars_update_;
  bool suppress_adjust_view_size_;
  bool allows_layout_invalidation_after_layout_clean_;

  Member<ElementVisibilityObserver> visibility_observer_;

  IntRect remote_viewport_intersection_;

  // For testing.
  struct ObjectPaintInvalidation {
    String name;
    PaintInvalidationReason reason;
  };
  std::unique_ptr<Vector<ObjectPaintInvalidation>>
      tracked_object_paint_invalidations_;

  // For Slimming Paint v2 only.
  std::unique_ptr<PaintController> paint_controller_;
  std::unique_ptr<PaintArtifactCompositor> paint_artifact_compositor_;

  bool is_storing_composited_layer_debug_info_;
  MainThreadScrollingReasons main_thread_scrolling_reasons_;

  // TODO(kenrb): Remove these when https://crbug.com/680606 is resolved.
  std::unique_ptr<CompositorAnimationTimeline> animation_timeline_;
  std::unique_ptr<CompositorAnimationHost> animation_host_;

  Member<PrintContext> print_context_;

  FRIEND_TEST_ALL_PREFIXES(WebViewTest, DeviceEmulationResetScrollbars);
};

inline void FrameView::IncrementVisuallyNonEmptyCharacterCount(unsigned count) {
  if (is_visually_non_empty_)
    return;
  visually_non_empty_character_count_ += count;
  // Use a threshold value to prevent very small amounts of visible content from
  // triggering didMeaningfulLayout.  The first few hundred characters rarely
  // contain the interesting content of the page.
  static const unsigned kVisualCharacterThreshold = 200;
  if (visually_non_empty_character_count_ > kVisualCharacterThreshold)
    SetIsVisuallyNonEmpty();
}

inline void FrameView::IncrementVisuallyNonEmptyPixelCount(
    const IntSize& size) {
  if (is_visually_non_empty_)
    return;
  visually_non_empty_pixel_count_ += size.Area();
  // Use a threshold value to prevent very small amounts of visible content from
  // triggering didMeaningfulLayout.
  static const unsigned kVisualPixelThreshold = 32 * 32;
  if (visually_non_empty_pixel_count_ > kVisualPixelThreshold)
    SetIsVisuallyNonEmpty();
}

DEFINE_TYPE_CASTS(FrameView,
                  PlatformFrameView,
                  platform_frame_view,
                  platform_frame_view->IsFrameView(),
                  platform_frame_view.IsFrameView());
DEFINE_TYPE_CASTS(FrameView,
                  FrameOrPlugin,
                  frame_or_plugin,
                  frame_or_plugin->IsFrameView(),
                  frame_or_plugin.IsFrameView());
DEFINE_TYPE_CASTS(FrameView,
                  ScrollableArea,
                  scrollableArea,
                  scrollableArea->IsFrameView(),
                  scrollableArea.IsFrameView());

}  // namespace blink

#endif  // FrameView_h
