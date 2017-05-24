/*
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2009 Antonio Gomes <tonikitoo@webkit.org>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/page/SpatialNavigation.h"

#include "core/HTMLNames.h"
#include "core/dom/NodeTraversal.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLAreaElement.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/layout/LayoutBox.h"
#include "core/layout/LayoutView.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "platform/geometry/IntRect.h"

namespace blink {

using namespace HTMLNames;

static void DeflateIfOverlapped(LayoutRect&, LayoutRect&);
static LayoutRect RectToAbsoluteCoordinates(LocalFrame* initial_frame,
                                            const LayoutRect&);
static bool IsScrollableNode(const Node*);

FocusCandidate::FocusCandidate(Node* node, WebFocusType type)
    : visible_node(nullptr),
      focusable_node(nullptr),
      enclosing_scrollable_box(nullptr),
      distance(MaxDistance()),
      is_offscreen(true),
      is_offscreen_after_scrolling(true) {
  DCHECK(node);
  DCHECK(node->IsElementNode());

  if (isHTMLAreaElement(*node)) {
    HTMLAreaElement& area = toHTMLAreaElement(*node);
    HTMLImageElement* image = area.ImageElement();
    if (!image || !image->GetLayoutObject())
      return;

    visible_node = image;
    rect = VirtualRectForAreaElementAndDirection(area, type);
  } else {
    if (!node->GetLayoutObject())
      return;

    visible_node = node;
    rect = NodeRectInAbsoluteCoordinates(node, true /* ignore border */);
  }

  focusable_node = node;
  is_offscreen = HasOffscreenRect(visible_node);
  is_offscreen_after_scrolling = HasOffscreenRect(visible_node, type);
}

bool IsSpatialNavigationEnabled(const LocalFrame* frame) {
  return (frame && frame->GetSettings() &&
          frame->GetSettings()->GetSpatialNavigationEnabled());
}

bool SpatialNavigationIgnoresEventHandlers(const LocalFrame* frame) {
  return (frame && frame->GetSettings() &&
          frame->GetSettings()->GetDeviceSupportsTouch());
}

static bool RectsIntersectOnOrthogonalAxis(WebFocusType type,
                                           const LayoutRect& a,
                                           const LayoutRect& b) {
  switch (type) {
    case kWebFocusTypeLeft:
    case kWebFocusTypeRight:
      return a.MaxY() > b.Y() && a.Y() < b.MaxY();
    case kWebFocusTypeUp:
    case kWebFocusTypeDown:
      return a.MaxX() > b.X() && a.X() < b.MaxX();
    default:
      NOTREACHED();
      return false;
  }
}

// Return true if rect |a| is below |b|. False otherwise.
// For overlapping rects, |a| is considered to be below |b|
// if both edges of |a| are below the respective ones of |b|
static inline bool Below(const LayoutRect& a, const LayoutRect& b) {
  return a.Y() >= b.MaxY() || (a.Y() >= b.Y() && a.MaxY() > b.MaxY() &&
                               a.X() < b.MaxX() && a.MaxX() > b.X());
}

// Return true if rect |a| is on the right of |b|. False otherwise.
// For overlapping rects, |a| is considered to be on the right of |b|
// if both edges of |a| are on the right of the respective ones of |b|
static inline bool RightOf(const LayoutRect& a, const LayoutRect& b) {
  return a.X() >= b.MaxX() || (a.X() >= b.X() && a.MaxX() > b.MaxX() &&
                               a.Y() < b.MaxY() && a.MaxY() > b.Y());
}

static bool IsRectInDirection(WebFocusType type,
                              const LayoutRect& cur_rect,
                              const LayoutRect& target_rect) {
  switch (type) {
    case kWebFocusTypeLeft:
      return RightOf(cur_rect, target_rect);
    case kWebFocusTypeRight:
      return RightOf(target_rect, cur_rect);
    case kWebFocusTypeUp:
      return Below(cur_rect, target_rect);
    case kWebFocusTypeDown:
      return Below(target_rect, cur_rect);
    default:
      NOTREACHED();
      return false;
  }
}

// Checks if |node| is offscreen the visible area (viewport) of its container
// document. In case it is, one can scroll in direction or take any different
// desired action later on.
bool HasOffscreenRect(Node* node, WebFocusType type) {
  // Get the FrameView in which |node| is (which means the current viewport if
  // |node| is not in an inner document), so we can check if its content rect is
  // visible before we actually move the focus to it.
  FrameView* frame_view = node->GetDocument().View();
  if (!frame_view)
    return true;

  DCHECK(!frame_view->NeedsLayout());

  LayoutRect container_viewport_rect(frame_view->VisibleContentRect());
  // We want to select a node if it is currently off screen, but will be
  // exposed after we scroll. Adjust the viewport to post-scrolling position.
  // If the container has overflow:hidden, we cannot scroll, so we do not pass
  // direction and we do not adjust for scrolling.
  int pixels_per_line_step =
      ScrollableArea::PixelsPerLineStep(frame_view->GetChromeClient());
  switch (type) {
    case kWebFocusTypeLeft:
      container_viewport_rect.SetX(container_viewport_rect.X() -
                                   pixels_per_line_step);
      container_viewport_rect.SetWidth(container_viewport_rect.Width() +
                                       pixels_per_line_step);
      break;
    case kWebFocusTypeRight:
      container_viewport_rect.SetWidth(container_viewport_rect.Width() +
                                       pixels_per_line_step);
      break;
    case kWebFocusTypeUp:
      container_viewport_rect.SetY(container_viewport_rect.Y() -
                                   pixels_per_line_step);
      container_viewport_rect.SetHeight(container_viewport_rect.Height() +
                                        pixels_per_line_step);
      break;
    case kWebFocusTypeDown:
      container_viewport_rect.SetHeight(container_viewport_rect.Height() +
                                        pixels_per_line_step);
      break;
    default:
      break;
  }

  LayoutObject* layout_object = node->GetLayoutObject();
  if (!layout_object)
    return true;

  LayoutRect rect(layout_object->AbsoluteVisualRect());
  if (rect.IsEmpty())
    return true;

  return !container_viewport_rect.Intersects(rect);
}

bool ScrollInDirection(LocalFrame* frame, WebFocusType type) {
  DCHECK(frame);

  if (frame && CanScrollInDirection(frame->GetDocument(), type)) {
    int dx = 0;
    int dy = 0;
    int pixels_per_line_step =
        ScrollableArea::PixelsPerLineStep(frame->View()->GetChromeClient());
    switch (type) {
      case kWebFocusTypeLeft:
        dx = -pixels_per_line_step;
        break;
      case kWebFocusTypeRight:
        dx = pixels_per_line_step;
        break;
      case kWebFocusTypeUp:
        dy = -pixels_per_line_step;
        break;
      case kWebFocusTypeDown:
        dy = pixels_per_line_step;
        break;
      default:
        NOTREACHED();
        return false;
    }

    frame->View()->ScrollBy(ScrollOffset(dx, dy), kUserScroll);
    return true;
  }
  return false;
}

bool ScrollInDirection(Node* container, WebFocusType type) {
  DCHECK(container);
  if (container->IsDocumentNode())
    return ScrollInDirection(ToDocument(container)->GetFrame(), type);

  if (!container->GetLayoutBox())
    return false;

  if (CanScrollInDirection(container, type)) {
    int dx = 0;
    int dy = 0;
    // TODO(leviw): Why are these values truncated (toInt) instead of rounding?
    FrameView* frame_view = container->GetDocument().View();
    int pixels_per_line_step = ScrollableArea::PixelsPerLineStep(
        frame_view ? frame_view->GetChromeClient() : nullptr);
    switch (type) {
      case kWebFocusTypeLeft:
        dx = -pixels_per_line_step;
        break;
      case kWebFocusTypeRight:
        DCHECK_GT(container->GetLayoutBox()->ScrollWidth(),
                  (container->GetLayoutBox()->ScrollLeft() +
                   container->GetLayoutBox()->ClientWidth()));
        dx = pixels_per_line_step;
        break;
      case kWebFocusTypeUp:
        dy = -pixels_per_line_step;
        break;
      case kWebFocusTypeDown:
        DCHECK(container->GetLayoutBox()->ScrollHeight() -
               (container->GetLayoutBox()->ScrollTop() +
                container->GetLayoutBox()->ClientHeight()));
        dy = pixels_per_line_step;
        break;
      default:
        NOTREACHED();
        return false;
    }

    container->GetLayoutBox()->ScrollByRecursively(ScrollOffset(dx, dy));
    return true;
  }

  return false;
}

static void DeflateIfOverlapped(LayoutRect& a, LayoutRect& b) {
  if (!a.Intersects(b) || a.Contains(b) || b.Contains(a))
    return;

  LayoutUnit deflate_factor = LayoutUnit(-FudgeFactor());

  // Avoid negative width or height values.
  if ((a.Width() + 2 * deflate_factor > 0) &&
      (a.Height() + 2 * deflate_factor > 0))
    a.Inflate(deflate_factor);

  if ((b.Width() + 2 * deflate_factor > 0) &&
      (b.Height() + 2 * deflate_factor > 0))
    b.Inflate(deflate_factor);
}

bool IsScrollableNode(const Node* node) {
  DCHECK(!node->IsDocumentNode());

  if (!node)
    return false;

  if (LayoutObject* layout_object = node->GetLayoutObject())
    return layout_object->IsBox() &&
           ToLayoutBox(layout_object)->CanBeScrolledAndHasScrollableArea() &&
           node->hasChildren();

  return false;
}

Node* ScrollableEnclosingBoxOrParentFrameForNodeInDirection(WebFocusType type,
                                                            Node* node) {
  DCHECK(node);
  Node* parent = node;
  do {
    // FIXME: Spatial navigation is broken for OOPI.
    if (parent->IsDocumentNode())
      parent = ToDocument(parent)->GetFrame()->DeprecatedLocalOwner();
    else
      parent = parent->ParentOrShadowHostNode();
  } while (parent && !CanScrollInDirection(parent, type) &&
           !parent->IsDocumentNode());

  return parent;
}

bool CanScrollInDirection(const Node* container, WebFocusType type) {
  DCHECK(container);
  if (container->IsDocumentNode())
    return CanScrollInDirection(ToDocument(container)->GetFrame(), type);

  if (!IsScrollableNode(container))
    return false;

  switch (type) {
    case kWebFocusTypeLeft:
      return (container->GetLayoutObject()->Style()->OverflowX() !=
                  EOverflow::kHidden &&
              container->GetLayoutBox()->ScrollLeft() > 0);
    case kWebFocusTypeUp:
      return (container->GetLayoutObject()->Style()->OverflowY() !=
                  EOverflow::kHidden &&
              container->GetLayoutBox()->ScrollTop() > 0);
    case kWebFocusTypeRight:
      return (container->GetLayoutObject()->Style()->OverflowX() !=
                  EOverflow::kHidden &&
              container->GetLayoutBox()->ScrollLeft() +
                      container->GetLayoutBox()->ClientWidth() <
                  container->GetLayoutBox()->ScrollWidth());
    case kWebFocusTypeDown:
      return (container->GetLayoutObject()->Style()->OverflowY() !=
                  EOverflow::kHidden &&
              container->GetLayoutBox()->ScrollTop() +
                      container->GetLayoutBox()->ClientHeight() <
                  container->GetLayoutBox()->ScrollHeight());
    default:
      NOTREACHED();
      return false;
  }
}

bool CanScrollInDirection(const LocalFrame* frame, WebFocusType type) {
  if (!frame->View())
    return false;
  LayoutView* layoutView = frame->ContentLayoutObject();
  if (!layoutView)
    return false;
  ScrollbarMode vertical_mode;
  ScrollbarMode horizontal_mode;
  layoutView->CalculateScrollbarModes(horizontal_mode, vertical_mode);
  if ((type == kWebFocusTypeLeft || type == kWebFocusTypeRight) &&
      kScrollbarAlwaysOff == horizontal_mode)
    return false;
  if ((type == kWebFocusTypeUp || type == kWebFocusTypeDown) &&
      kScrollbarAlwaysOff == vertical_mode)
    return false;
  LayoutSize size(frame->View()->ContentsSize());
  LayoutSize offset(frame->View()->ScrollOffsetInt());
  LayoutRect rect(frame->View()->VisibleContentRect(kIncludeScrollbars));

  switch (type) {
    case kWebFocusTypeLeft:
      return offset.Width() > 0;
    case kWebFocusTypeUp:
      return offset.Height() > 0;
    case kWebFocusTypeRight:
      return rect.Width() + offset.Width() < size.Width();
    case kWebFocusTypeDown:
      return rect.Height() + offset.Height() < size.Height();
    default:
      NOTREACHED();
      return false;
  }
}

static LayoutRect RectToAbsoluteCoordinates(LocalFrame* initial_frame,
                                            const LayoutRect& initial_rect) {
  LayoutRect rect = initial_rect;
  for (Frame* frame = initial_frame; frame; frame = frame->Tree().Parent()) {
    if (!frame->IsLocalFrame())
      continue;
    // FIXME: Spatial navigation is broken for OOPI.
    Element* element = frame->DeprecatedLocalOwner();
    if (element) {
      do {
        rect.Move(element->OffsetLeft(), element->OffsetTop());
        LayoutObject* layout_object = element->GetLayoutObject();
        element = layout_object ? layout_object->OffsetParent() : nullptr;
      } while (element);
      rect.Move((-ToLocalFrame(frame)->View()->ScrollOffsetInt()));
    }
  }
  return rect;
}

LayoutRect NodeRectInAbsoluteCoordinates(Node* node, bool ignore_border) {
  DCHECK(node);
  DCHECK(node->GetLayoutObject());
  DCHECK(!node->GetDocument().View()->NeedsLayout());

  if (node->IsDocumentNode())
    return FrameRectInAbsoluteCoordinates(ToDocument(node)->GetFrame());
  LayoutRect rect = RectToAbsoluteCoordinates(node->GetDocument().GetFrame(),
                                              node->BoundingBox());

  // For authors that use border instead of outline in their CSS, we compensate
  // by ignoring the border when calculating the rect of the focused element.
  if (ignore_border) {
    rect.Move(node->GetLayoutObject()->Style()->BorderLeftWidth(),
              node->GetLayoutObject()->Style()->BorderTopWidth());
    rect.SetWidth(LayoutUnit(
        rect.Width() - node->GetLayoutObject()->Style()->BorderLeftWidth() -
        node->GetLayoutObject()->Style()->BorderRightWidth()));
    rect.SetHeight(LayoutUnit(
        rect.Height() - node->GetLayoutObject()->Style()->BorderTopWidth() -
        node->GetLayoutObject()->Style()->BorderBottomWidth()));
  }
  return rect;
}

LayoutRect FrameRectInAbsoluteCoordinates(LocalFrame* frame) {
  return RectToAbsoluteCoordinates(
      frame, LayoutRect(frame->View()->VisibleContentRect()));
}

// This method calculates the exitPoint from the startingRect and the entryPoint
// into the candidate rect.  The line between those 2 points is the closest
// distance between the 2 rects.  Takes care of overlapping rects, defining
// points so that the distance between them is zero where necessary
void EntryAndExitPointsForDirection(WebFocusType type,
                                    const LayoutRect& starting_rect,
                                    const LayoutRect& potential_rect,
                                    LayoutPoint& exit_point,
                                    LayoutPoint& entry_point) {
  switch (type) {
    case kWebFocusTypeLeft:
      exit_point.SetX(starting_rect.X());
      if (potential_rect.MaxX() < starting_rect.X())
        entry_point.SetX(potential_rect.MaxX());
      else
        entry_point.SetX(starting_rect.X());
      break;
    case kWebFocusTypeUp:
      exit_point.SetY(starting_rect.Y());
      if (potential_rect.MaxY() < starting_rect.Y())
        entry_point.SetY(potential_rect.MaxY());
      else
        entry_point.SetY(starting_rect.Y());
      break;
    case kWebFocusTypeRight:
      exit_point.SetX(starting_rect.MaxX());
      if (potential_rect.X() > starting_rect.MaxX())
        entry_point.SetX(potential_rect.X());
      else
        entry_point.SetX(starting_rect.MaxX());
      break;
    case kWebFocusTypeDown:
      exit_point.SetY(starting_rect.MaxY());
      if (potential_rect.Y() > starting_rect.MaxY())
        entry_point.SetY(potential_rect.Y());
      else
        entry_point.SetY(starting_rect.MaxY());
      break;
    default:
      NOTREACHED();
  }

  switch (type) {
    case kWebFocusTypeLeft:
    case kWebFocusTypeRight:
      if (Below(starting_rect, potential_rect)) {
        exit_point.SetY(starting_rect.Y());
        if (potential_rect.MaxY() < starting_rect.Y())
          entry_point.SetY(potential_rect.MaxY());
        else
          entry_point.SetY(starting_rect.Y());
      } else if (Below(potential_rect, starting_rect)) {
        exit_point.SetY(starting_rect.MaxY());
        if (potential_rect.Y() > starting_rect.MaxY())
          entry_point.SetY(potential_rect.Y());
        else
          entry_point.SetY(starting_rect.MaxY());
      } else {
        exit_point.SetY(max(starting_rect.Y(), potential_rect.Y()));
        entry_point.SetY(exit_point.Y());
      }
      break;
    case kWebFocusTypeUp:
    case kWebFocusTypeDown:
      if (RightOf(starting_rect, potential_rect)) {
        exit_point.SetX(starting_rect.X());
        if (potential_rect.MaxX() < starting_rect.X())
          entry_point.SetX(potential_rect.MaxX());
        else
          entry_point.SetX(starting_rect.X());
      } else if (RightOf(potential_rect, starting_rect)) {
        exit_point.SetX(starting_rect.MaxX());
        if (potential_rect.X() > starting_rect.MaxX())
          entry_point.SetX(potential_rect.X());
        else
          entry_point.SetX(starting_rect.MaxX());
      } else {
        exit_point.SetX(max(starting_rect.X(), potential_rect.X()));
        entry_point.SetX(exit_point.X());
      }
      break;
    default:
      NOTREACHED();
  }
}

bool AreElementsOnSameLine(const FocusCandidate& first_candidate,
                           const FocusCandidate& second_candidate) {
  if (first_candidate.IsNull() || second_candidate.IsNull())
    return false;

  if (!first_candidate.visible_node->GetLayoutObject() ||
      !second_candidate.visible_node->GetLayoutObject())
    return false;

  if (!first_candidate.rect.Intersects(second_candidate.rect))
    return false;

  if (isHTMLAreaElement(*first_candidate.focusable_node) ||
      isHTMLAreaElement(*second_candidate.focusable_node))
    return false;

  if (!first_candidate.visible_node->GetLayoutObject()->IsLayoutInline() ||
      !second_candidate.visible_node->GetLayoutObject()->IsLayoutInline())
    return false;

  if (first_candidate.visible_node->GetLayoutObject()->ContainingBlock() !=
      second_candidate.visible_node->GetLayoutObject()->ContainingBlock())
    return false;

  return true;
}

void DistanceDataForNode(WebFocusType type,
                         const FocusCandidate& current,
                         FocusCandidate& candidate) {
  if (!IsRectInDirection(type, current.rect, candidate.rect))
    return;

  if (AreElementsOnSameLine(current, candidate)) {
    if ((type == kWebFocusTypeUp && current.rect.Y() > candidate.rect.Y()) ||
        (type == kWebFocusTypeDown && candidate.rect.Y() > current.rect.Y())) {
      candidate.distance = 0;
      return;
    }
  }

  LayoutRect node_rect = candidate.rect;
  LayoutRect current_rect = current.rect;
  DeflateIfOverlapped(current_rect, node_rect);

  LayoutPoint exit_point;
  LayoutPoint entry_point;
  EntryAndExitPointsForDirection(type, current_rect, node_rect, exit_point,
                                 entry_point);

  LayoutUnit x_axis = (exit_point.X() - entry_point.X()).Abs();
  LayoutUnit y_axis = (exit_point.Y() - entry_point.Y()).Abs();

  LayoutUnit navigation_axis_distance;
  LayoutUnit weighted_orthogonal_axis_distance;

  // Bias and weights are put to the orthogonal axis distance calculation
  // so aligned candidates would have advantage over partially-aligned ones
  // and then over not-aligned candidates. The bias is given to not-aligned
  // candidates with respect to size of the current rect. The weight for
  // left/right direction is given a higher value to allow navigation on
  // common horizonally-aligned elements. The hardcoded values are based on
  // tests and experiments.
  const int kOrthogonalWeightForLeftRight = 30;
  const int kOrthogonalWeightForUpDown = 2;
  int orthogonal_bias = 0;

  switch (type) {
    case kWebFocusTypeLeft:
    case kWebFocusTypeRight:
      navigation_axis_distance = x_axis;
      if (!RectsIntersectOnOrthogonalAxis(type, current_rect, node_rect))
        orthogonal_bias = (current_rect.Height() / 2).ToInt();
      weighted_orthogonal_axis_distance =
          (y_axis + orthogonal_bias) * kOrthogonalWeightForLeftRight;
      break;
    case kWebFocusTypeUp:
    case kWebFocusTypeDown:
      navigation_axis_distance = y_axis;
      if (!RectsIntersectOnOrthogonalAxis(type, current_rect, node_rect))
        orthogonal_bias = (current_rect.Width() / 2).ToInt();
      weighted_orthogonal_axis_distance =
          (x_axis + orthogonal_bias) * kOrthogonalWeightForUpDown;
      break;
    default:
      NOTREACHED();
      return;
  }

  double euclidian_distance_pow2 =
      (x_axis * x_axis + y_axis * y_axis).ToDouble();
  LayoutRect intersection_rect = Intersection(current_rect, node_rect);
  double overlap =
      (intersection_rect.Width() * intersection_rect.Height()).ToDouble();

  // Distance calculation is based on http://www.w3.org/TR/WICD/#focus-handling
  candidate.distance = sqrt(euclidian_distance_pow2) +
                       navigation_axis_distance +
                       weighted_orthogonal_axis_distance - sqrt(overlap);
}

bool CanBeScrolledIntoView(WebFocusType type, const FocusCandidate& candidate) {
  DCHECK(candidate.visible_node);
  DCHECK(candidate.is_offscreen);
  LayoutRect candidate_rect = candidate.rect;
  for (Node& parent_node :
       NodeTraversal::AncestorsOf(*candidate.visible_node)) {
    LayoutRect parent_rect = NodeRectInAbsoluteCoordinates(&parent_node);
    if (!candidate_rect.Intersects(parent_rect)) {
      if (((type == kWebFocusTypeLeft || type == kWebFocusTypeRight) &&
           parent_node.GetLayoutObject()->Style()->OverflowX() ==
               EOverflow::kHidden) ||
          ((type == kWebFocusTypeUp || type == kWebFocusTypeDown) &&
           parent_node.GetLayoutObject()->Style()->OverflowY() ==
               EOverflow::kHidden))
        return false;
    }
    if (parent_node == candidate.enclosing_scrollable_box)
      return CanScrollInDirection(&parent_node, type);
  }
  return true;
}

// The starting rect is the rect of the focused node, in document coordinates.
// Compose a virtual starting rect if there is no focused node or if it is off
// screen.  The virtual rect is the edge of the container or frame. We select
// which edge depending on the direction of the navigation.
LayoutRect VirtualRectForDirection(WebFocusType type,
                                   const LayoutRect& starting_rect,
                                   LayoutUnit width) {
  LayoutRect virtual_starting_rect = starting_rect;
  switch (type) {
    case kWebFocusTypeLeft:
      virtual_starting_rect.SetX(virtual_starting_rect.MaxX() - width);
      virtual_starting_rect.SetWidth(width);
      break;
    case kWebFocusTypeUp:
      virtual_starting_rect.SetY(virtual_starting_rect.MaxY() - width);
      virtual_starting_rect.SetHeight(width);
      break;
    case kWebFocusTypeRight:
      virtual_starting_rect.SetWidth(width);
      break;
    case kWebFocusTypeDown:
      virtual_starting_rect.SetHeight(width);
      break;
    default:
      NOTREACHED();
  }

  return virtual_starting_rect;
}

LayoutRect VirtualRectForAreaElementAndDirection(HTMLAreaElement& area,
                                                 WebFocusType type) {
  DCHECK(area.ImageElement());
  // Area elements tend to overlap more than other focusable elements. We
  // flatten the rect of the area elements to minimize the effect of overlapping
  // areas.
  LayoutRect rect = VirtualRectForDirection(
      type,
      RectToAbsoluteCoordinates(
          area.GetDocument().GetFrame(),
          area.ComputeAbsoluteRect(area.ImageElement()->GetLayoutObject())),
      LayoutUnit(1));
  return rect;
}

HTMLFrameOwnerElement* FrameOwnerElement(FocusCandidate& candidate) {
  return candidate.IsFrameOwnerElement()
             ? ToHTMLFrameOwnerElement(candidate.visible_node)
             : nullptr;
};

}  // namespace blink
