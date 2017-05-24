// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/page/scrolling/TopDocumentRootScrollerController.h"

#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/frame/FrameView.h"
#include "core/frame/PageScaleConstraintsSet.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/layout/LayoutView.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "core/page/scrolling/OverscrollController.h"
#include "core/page/scrolling/RootScrollerUtil.h"
#include "core/page/scrolling/ViewportScrollCallback.h"
#include "core/paint/PaintLayer.h"
#include "platform/scroll/ScrollableArea.h"

namespace blink {

// static
TopDocumentRootScrollerController* TopDocumentRootScrollerController::Create(
    Page& page) {
  return new TopDocumentRootScrollerController(page);
}

TopDocumentRootScrollerController::TopDocumentRootScrollerController(Page& page)
    : page_(&page) {}

DEFINE_TRACE(TopDocumentRootScrollerController) {
  visitor->Trace(viewport_apply_scroll_);
  visitor->Trace(global_root_scroller_);
  visitor->Trace(page_);
}

void TopDocumentRootScrollerController::DidChangeRootScroller() {
  RecomputeGlobalRootScroller();
}

void TopDocumentRootScrollerController::DidResizeViewport() {
  if (!GlobalRootScroller())
    return;

  // Top controls can resize the viewport without invalidating compositing or
  // paint so we need to do that manually here.
  GlobalRootScroller()->SetNeedsCompositingUpdate();

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled()) {
    if (GlobalRootScroller()->GetLayoutObject())
      GlobalRootScroller()->GetLayoutObject()->SetNeedsPaintPropertyUpdate();
  }
}

ScrollableArea* TopDocumentRootScrollerController::RootScrollerArea() const {
  return RootScrollerUtil::ScrollableAreaForRootScroller(GlobalRootScroller());
}

IntSize TopDocumentRootScrollerController::RootScrollerVisibleArea() const {
  if (!TopDocument() || !TopDocument()->View())
    return IntSize();

  float minimum_page_scale =
      page_->GetPageScaleConstraintsSet().FinalConstraints().minimum_scale;
  int browser_controls_adjustment =
      ceilf(page_->GetVisualViewport().BrowserControlsAdjustment() /
            minimum_page_scale);

  return TopDocument()->View()->VisibleContentSize(kExcludeScrollbars) +
         IntSize(0, browser_controls_adjustment);
}

Element* TopDocumentRootScrollerController::FindGlobalRootScrollerElement() {
  if (!TopDocument())
    return nullptr;

  Node* effective_root_scroller =
      &TopDocument()->GetRootScrollerController().EffectiveRootScroller();

  if (effective_root_scroller->IsDocumentNode())
    return TopDocument()->documentElement();

  DCHECK(effective_root_scroller->IsElementNode());
  Element* element = ToElement(effective_root_scroller);

  while (element && element->IsFrameOwnerElement()) {
    HTMLFrameOwnerElement* frame_owner = ToHTMLFrameOwnerElement(element);
    DCHECK(frame_owner);

    Document* iframe_document = frame_owner->contentDocument();
    if (!iframe_document)
      return element;

    effective_root_scroller =
        &iframe_document->GetRootScrollerController().EffectiveRootScroller();
    if (effective_root_scroller->IsDocumentNode())
      return iframe_document->documentElement();

    element = ToElement(effective_root_scroller);
  }

  return element;
}

void TopDocumentRootScrollerController::RecomputeGlobalRootScroller() {
  if (!viewport_apply_scroll_)
    return;

  Element* target = FindGlobalRootScrollerElement();
  if (target == global_root_scroller_)
    return;

  ScrollableArea* target_scroller =
      RootScrollerUtil::ScrollableAreaForRootScroller(target);

  if (!target_scroller)
    return;

  if (global_root_scroller_)
    global_root_scroller_->RemoveApplyScroll();

  // Use disable-native-scroll since the ViewportScrollCallback needs to
  // apply scroll actions both before (BrowserControls) and after (overscroll)
  // scrolling the element so it will apply scroll to the element itself.
  target->setApplyScroll(viewport_apply_scroll_, "disable-native-scroll");

  ScrollableArea* old_root_scroller_area =
      RootScrollerUtil::ScrollableAreaForRootScroller(global_root_scroller_);

  global_root_scroller_ = target;

  // Ideally, scroll customization would pass the current element to scroll to
  // the apply scroll callback but this doesn't happen today so we set it
  // through a back door here. This is also needed by the
  // ViewportScrollCallback to swap the target into the layout viewport
  // in RootFrameViewport.
  viewport_apply_scroll_->SetScroller(target_scroller);

  if (old_root_scroller_area)
    old_root_scroller_area->DidChangeGlobalRootScroller();

  target_scroller->DidChangeGlobalRootScroller();
}

Document* TopDocumentRootScrollerController::TopDocument() const {
  if (!page_ || !page_->MainFrame() || !page_->MainFrame()->IsLocalFrame())
    return nullptr;

  return ToLocalFrame(page_->MainFrame())->GetDocument();
}

void TopDocumentRootScrollerController::DidUpdateCompositing() {
  if (!page_)
    return;

  // Let the compositor-side counterpart know about this change.
  page_->GetChromeClient().RegisterViewportLayers();
}

void TopDocumentRootScrollerController::DidDisposeScrollableArea(
    ScrollableArea& area) {
  if (!TopDocument() || !TopDocument()->View())
    return;

  // If the document is tearing down, we may no longer have a layoutViewport to
  // fallback to.
  if (TopDocument()->Lifecycle().GetState() >= DocumentLifecycle::kStopping)
    return;

  FrameView* frame_view = TopDocument()->View();

  RootFrameViewport* rfv = frame_view->GetRootFrameViewport();

  if (&area == &rfv->LayoutViewport()) {
    DCHECK(frame_view->LayoutViewportScrollableArea());
    rfv->SetLayoutViewport(*frame_view->LayoutViewportScrollableArea());
  }
}

void TopDocumentRootScrollerController::InitializeViewportScrollCallback(
    RootFrameViewport& root_frame_viewport) {
  DCHECK(page_);
  viewport_apply_scroll_ = ViewportScrollCallback::Create(
      &page_->GetBrowserControls(), &page_->GetOverscrollController(),
      root_frame_viewport);

  RecomputeGlobalRootScroller();
}

bool TopDocumentRootScrollerController::IsViewportScrollCallback(
    const ScrollStateCallback* callback) const {
  if (!callback)
    return false;

  return callback == viewport_apply_scroll_.Get();
}

GraphicsLayer* TopDocumentRootScrollerController::RootScrollerLayer() const {
  ScrollableArea* area =
      RootScrollerUtil::ScrollableAreaForRootScroller(global_root_scroller_);

  if (!area)
    return nullptr;

  GraphicsLayer* graphics_layer = area->LayerForScrolling();

  // TODO(bokan): We should assert graphicsLayer here and
  // RootScrollerController should do whatever needs to happen to ensure
  // the root scroller gets composited.

  return graphics_layer;
}

GraphicsLayer* TopDocumentRootScrollerController::RootContainerLayer() const {
  ScrollableArea* area =
      RootScrollerUtil::ScrollableAreaForRootScroller(global_root_scroller_);

  return area ? area->LayerForContainer() : nullptr;
}

PaintLayer* TopDocumentRootScrollerController::RootScrollerPaintLayer() const {
  return RootScrollerUtil::PaintLayerForRootScroller(global_root_scroller_);
}

Element* TopDocumentRootScrollerController::GlobalRootScroller() const {
  return global_root_scroller_.Get();
}

}  // namespace blink
