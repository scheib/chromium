// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/page/scrolling/RootScrollerController.h"

#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/frame/FrameView.h"
#include "core/layout/LayoutBox.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/page/Page.h"
#include "core/page/scrolling/RootScrollerUtil.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/PaintLayerScrollableArea.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/scroll/ScrollableArea.h"

namespace blink {

class RootFrameViewport;

namespace {

bool FillsViewport(const Element& element) {
  DCHECK(element.GetLayoutObject());
  DCHECK(element.GetLayoutObject()->IsBox());

  LayoutObject* layout_object = element.GetLayoutObject();

  // TODO(bokan): Broken for OOPIF. crbug.com/642378.
  Document& top_document = element.GetDocument().TopDocument();

  Vector<FloatQuad> quads;
  layout_object->AbsoluteQuads(quads);
  DCHECK_EQ(quads.size(), 1u);

  if (!quads[0].IsRectilinear())
    return false;

  LayoutRect bounding_box(quads[0].BoundingBox());

  return bounding_box.Location() == LayoutPoint::Zero() &&
         bounding_box.Size() == top_document.GetLayoutViewItem().Size();
}

}  // namespace

// static
RootScrollerController* RootScrollerController::Create(Document& document) {
  return new RootScrollerController(document);
}

RootScrollerController::RootScrollerController(Document& document)
    : document_(&document),
      effective_root_scroller_(&document),
      document_has_document_element_(false) {}

DEFINE_TRACE(RootScrollerController) {
  visitor->Trace(document_);
  visitor->Trace(root_scroller_);
  visitor->Trace(effective_root_scroller_);
}

void RootScrollerController::Set(Element* new_root_scroller) {
  root_scroller_ = new_root_scroller;
  RecomputeEffectiveRootScroller();
}

Element* RootScrollerController::Get() const {
  return root_scroller_;
}

Node& RootScrollerController::EffectiveRootScroller() const {
  DCHECK(effective_root_scroller_);
  return *effective_root_scroller_;
}

void RootScrollerController::DidUpdateLayout() {
  RecomputeEffectiveRootScroller();
}

void RootScrollerController::RecomputeEffectiveRootScroller() {
  bool root_scroller_valid =
      root_scroller_ && IsValidRootScroller(*root_scroller_);

  Node* new_effective_root_scroller = document_;
  if (root_scroller_valid)
    new_effective_root_scroller = root_scroller_;

  // TODO(bokan): This is a terrible hack but required because the viewport
  // apply scroll works on Elements rather than Nodes. If we're going from
  // !documentElement to documentElement, we can't early out even if the root
  // scroller didn't change since the global root scroller didn't have an
  // Element previously to put it's ViewportScrollCallback onto. We need this
  // to kick the global root scroller to recompute itself. We can remove this
  // if ScrollCustomization is moved to the Node rather than Element.
  bool old_has_document_element = document_has_document_element_;
  document_has_document_element_ = document_->documentElement();

  if (old_has_document_element || !document_has_document_element_) {
    if (effective_root_scroller_ == new_effective_root_scroller)
      return;
  }

  effective_root_scroller_ = new_effective_root_scroller;

  if (Page* page = document_->GetPage())
    page->GlobalRootScrollerController().DidChangeRootScroller();
}

bool RootScrollerController::IsValidRootScroller(const Element& element) const {
  if (!element.GetLayoutObject())
    return false;

  if (!element.GetLayoutObject()->HasOverflowClip() &&
      !element.IsFrameOwnerElement())
    return false;

  if (!RootScrollerUtil::ScrollableAreaForRootScroller(&element))
    return false;

  if (!FillsViewport(element))
    return false;

  return true;
}

PaintLayer* RootScrollerController::RootScrollerPaintLayer() const {
  return RootScrollerUtil::PaintLayerForRootScroller(effective_root_scroller_);
}

bool RootScrollerController::ScrollsViewport(const Element& element) const {
  if (effective_root_scroller_->IsDocumentNode())
    return element == document_->documentElement();

  return element == effective_root_scroller_.Get();
}

}  // namespace blink
