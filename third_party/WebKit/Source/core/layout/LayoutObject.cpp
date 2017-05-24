/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 *           (C) 2004 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2011 Apple Inc.
 *               All rights reserved.
 * Copyright (C) 2009 Google Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved.
 *               (http://www.torchmobile.com/)
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

#include "core/layout/LayoutObject.h"

#include <algorithm>
#include <memory>

#include "core/animation/ElementAnimations.h"
#include "core/css/resolver/StyleResolver.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/StyleChangeReason.h"
#include "core/dom/StyleEngine.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/TextAffinity.h"
#include "core/editing/VisibleUnits.h"
#include "core/frame/DeprecatedScheduleStyleRecalcDuringLayout.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLElement.h"
#include "core/html/HTMLHtmlElement.h"
#include "core/html/HTMLTableCellElement.h"
#include "core/html/HTMLTableElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutCounter.h"
#include "core/layout/LayoutDeprecatedFlexibleBox.h"
#include "core/layout/LayoutFlexibleBox.h"
#include "core/layout/LayoutFlowThread.h"
#include "core/layout/LayoutGrid.h"
#include "core/layout/LayoutImage.h"
#include "core/layout/LayoutImageResourceStyleImage.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutListItem.h"
#include "core/layout/LayoutMultiColumnSpannerPlaceholder.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutScrollbarPart.h"
#include "core/layout/LayoutTableCaption.h"
#include "core/layout/LayoutTableCell.h"
#include "core/layout/LayoutTableCol.h"
#include "core/layout/LayoutTableRow.h"
#include "core/layout/LayoutTheme.h"
#include "core/layout/LayoutView.h"
#include "core/layout/PaintInvalidationState.h"
#include "core/layout/api/LayoutAPIShim.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/ng/layout_ng_block_flow.h"
#include "core/page/AutoscrollController.h"
#include "core/page/Page.h"
#include "core/paint/ObjectPaintInvalidator.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/RarePaintData.h"
#include "core/style/ContentData.h"
#include "core/style/CursorData.h"
#include "platform/InstanceCounters.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/geometry/TransformState.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/TouchAction.h"
#include "platform/graphics/paint/PropertyTreeState.h"
#include "platform/instrumentation/tracing/TracedValue.h"
#include "platform/wtf/allocator/Partitions.h"
#include "platform/wtf/text/StringBuilder.h"
#include "platform/wtf/text/WTFString.h"
#ifndef NDEBUG
#include <stdio.h>
#endif

namespace blink {

namespace {

static bool g_modify_layout_tree_structure_any_state = false;

}  // namespace

#if DCHECK_IS_ON()

LayoutObject::SetLayoutNeededForbiddenScope::SetLayoutNeededForbiddenScope(
    LayoutObject& layout_object)
    : layout_object_(layout_object),
      preexisting_forbidden_(layout_object_.IsSetNeedsLayoutForbidden()) {
  layout_object_.SetNeedsLayoutIsForbidden(true);
}

LayoutObject::SetLayoutNeededForbiddenScope::~SetLayoutNeededForbiddenScope() {
  layout_object_.SetNeedsLayoutIsForbidden(preexisting_forbidden_);
}
#endif

struct SameSizeAsLayoutObject : DisplayItemClient {
  virtual ~SameSizeAsLayoutObject() {}  // Allocate vtable pointer.
  void* pointers[5];
  Member<void*> members[1];
#if DCHECK_IS_ON()
  unsigned debug_bitfields_ : 2;
#endif
  unsigned bitfields_;
  unsigned bitfields2_;
  LayoutRect visual_rect_;
  LayoutPoint paint_offset_;
  std::unique_ptr<void*> rare_paint_data_;
};

static_assert(sizeof(LayoutObject) == sizeof(SameSizeAsLayoutObject),
              "LayoutObject should stay small");

bool LayoutObject::affects_parent_block_ = false;

void* LayoutObject::operator new(size_t sz) {
  DCHECK(IsMainThread());
  return PartitionAlloc(WTF::Partitions::LayoutPartition(), sz,
                        WTF_HEAP_PROFILER_TYPE_NAME(LayoutObject));
}

void LayoutObject::operator delete(void* ptr) {
  DCHECK(IsMainThread());
  WTF::PartitionFree(ptr);
}

LayoutObject* LayoutObject::CreateObject(Element* element,
                                         const ComputedStyle& style) {
  DCHECK(IsAllowedToModifyLayoutTreeStructure(element->GetDocument()));

  // Minimal support for content properties replacing an entire element.
  // Works only if we have exactly one piece of content and it's a URL.
  // Otherwise acts as if we didn't support this feature.
  const ContentData* content_data = style.GetContentData();
  if (content_data && !content_data->Next() && content_data->IsImage() &&
      !element->IsPseudoElement()) {
    LayoutImage* image = new LayoutImage(element);
    // LayoutImageResourceStyleImage requires a style being present on the image
    // but we don't want to trigger a style change now as the node is not fully
    // attached. Moving this code to style change doesn't make sense as it
    // should be run once at layoutObject creation.
    image->SetStyleInternal(const_cast<ComputedStyle*>(&style));
    if (const StyleImage* style_image =
            ToImageContentData(content_data)->GetImage()) {
      image->SetImageResource(LayoutImageResourceStyleImage::Create(
          const_cast<StyleImage*>(style_image)));
      image->SetIsGeneratedContent();
    } else {
      image->SetImageResource(LayoutImageResource::Create());
    }
    image->SetStyleInternal(nullptr);
    return image;
  }

  switch (style.Display()) {
    case EDisplay::kNone:
    case EDisplay::kContents:
      return nullptr;
    case EDisplay::kInline:
      return new LayoutInline(element);
    case EDisplay::kBlock:
    case EDisplay::kFlowRoot:
    case EDisplay::kInlineBlock:
      if (RuntimeEnabledFeatures::layoutNGEnabled())
        return new LayoutNGBlockFlow(element);
      return new LayoutBlockFlow(element);
    case EDisplay::kListItem:
      return new LayoutListItem(element);
    case EDisplay::kTable:
    case EDisplay::kInlineTable:
      return new LayoutTable(element);
    case EDisplay::kTableRowGroup:
    case EDisplay::kTableHeaderGroup:
    case EDisplay::kTableFooterGroup:
      return new LayoutTableSection(element);
    case EDisplay::kTableRow:
      return new LayoutTableRow(element);
    case EDisplay::kTableColumnGroup:
    case EDisplay::kTableColumn:
      return new LayoutTableCol(element);
    case EDisplay::kTableCell:
      return new LayoutTableCell(element);
    case EDisplay::kTableCaption:
      return new LayoutTableCaption(element);
    case EDisplay::kWebkitBox:
    case EDisplay::kWebkitInlineBox:
      return new LayoutDeprecatedFlexibleBox(*element);
    case EDisplay::kFlex:
    case EDisplay::kInlineFlex:
      return new LayoutFlexibleBox(element);
    case EDisplay::kGrid:
    case EDisplay::kInlineGrid:
      return new LayoutGrid(element);
  }

  NOTREACHED();
  return nullptr;
}

LayoutObject::LayoutObject(Node* node)
    : style_(nullptr),
      node_(node),
      parent_(nullptr),
      previous_(nullptr),
      next_(nullptr),
#if DCHECK_IS_ON()
      has_ax_object_(false),
      set_needs_layout_forbidden_(false),
#endif
      bitfields_(node) {
  InstanceCounters::IncrementCounter(InstanceCounters::kLayoutObjectCounter);
  if (node_)
    GetFrameView()->IncrementLayoutObjectCount();
}

LayoutObject::~LayoutObject() {
#if DCHECK_IS_ON()
  DCHECK(!has_ax_object_);
#endif
  InstanceCounters::DecrementCounter(InstanceCounters::kLayoutObjectCounter);
}

bool LayoutObject::IsDescendantOf(const LayoutObject* obj) const {
  for (const LayoutObject* r = this; r; r = r->parent_) {
    if (r == obj)
      return true;
  }
  return false;
}

bool LayoutObject::IsHR() const {
  return isHTMLHRElement(GetNode());
}

bool LayoutObject::IsLegend() const {
  return isHTMLLegendElement(GetNode());
}

void LayoutObject::SetIsInsideFlowThreadIncludingDescendants(
    bool inside_flow_thread) {
  LayoutObject* next;
  for (LayoutObject* object = this; object; object = next) {
    // If object is a fragmentation context it already updated the descendants
    // flag accordingly.
    if (object->IsLayoutFlowThread()) {
      next = object->NextInPreOrderAfterChildren(this);
      continue;
    }
    next = object->NextInPreOrder(this);
    DCHECK_NE(inside_flow_thread, object->IsInsideFlowThread());
    object->SetIsInsideFlowThread(inside_flow_thread);
  }
}

bool LayoutObject::RequiresAnonymousTableWrappers(
    const LayoutObject* new_child) const {
  // Check should agree with:
  // CSS 2.1 Tables: 17.2.1 Anonymous table objects
  // http://www.w3.org/TR/CSS21/tables.html#anonymous-boxes
  if (new_child->IsLayoutTableCol()) {
    const LayoutTableCol* new_table_column = ToLayoutTableCol(new_child);
    bool is_column_in_column_group =
        new_table_column->IsTableColumn() && IsLayoutTableCol();
    return !IsTable() && !is_column_in_column_group;
  }
  if (new_child->IsTableCaption())
    return !IsTable();
  if (new_child->IsTableSection())
    return !IsTable();
  if (new_child->IsTableRow())
    return !IsTableSection();
  if (new_child->IsTableCell())
    return !IsTableRow();
  return false;
}

DISABLE_CFI_PERF
void LayoutObject::AddChild(LayoutObject* new_child,
                            LayoutObject* before_child) {
  DCHECK(IsAllowedToModifyLayoutTreeStructure(GetDocument()));

  LayoutObjectChildList* children = VirtualChildren();
  DCHECK(children);
  if (!children)
    return;

  if (RequiresAnonymousTableWrappers(new_child)) {
    // Generate an anonymous table or reuse existing one from previous child
    // Per: 17.2.1 Anonymous table objects 3. Generate missing parents
    // http://www.w3.org/TR/CSS21/tables.html#anonymous-boxes
    LayoutTable* table;
    LayoutObject* after_child =
        before_child ? before_child->PreviousSibling() : children->LastChild();
    if (after_child && after_child->IsAnonymous() && after_child->IsTable() &&
        !after_child->IsBeforeContent()) {
      table = ToLayoutTable(after_child);
    } else {
      table = LayoutTable::CreateAnonymousWithParent(this);
      children->InsertChildNode(this, table, before_child);
    }
    table->AddChild(new_child);
  } else {
    children->InsertChildNode(this, new_child, before_child);
  }

  if (new_child->IsText() &&
      new_child->Style()->TextTransform() == ETextTransform::kCapitalize)
    ToLayoutText(new_child)->TransformText();
}

void LayoutObject::RemoveChild(LayoutObject* old_child) {
  DCHECK(IsAllowedToModifyLayoutTreeStructure(GetDocument()));

  LayoutObjectChildList* children = VirtualChildren();
  DCHECK(children);
  if (!children)
    return;

  children->RemoveChildNode(this, old_child);
}

void LayoutObject::SetDangerousOneWayParent(LayoutObject* parent) {
  DCHECK(!PreviousSibling());
  DCHECK(!NextSibling());
  DCHECK(!parent || !parent_);
  SetParent(parent);
}

void LayoutObject::RegisterSubtreeChangeListenerOnDescendants(bool value) {
  // If we're set to the same value then we're done as that means it's
  // set down the tree that way already.
  if (bitfields_.SubtreeChangeListenerRegistered() == value)
    return;

  bitfields_.SetSubtreeChangeListenerRegistered(value);

  for (LayoutObject* curr = SlowFirstChild(); curr; curr = curr->NextSibling())
    curr->RegisterSubtreeChangeListenerOnDescendants(value);
}

void LayoutObject::NotifyAncestorsOfSubtreeChange() {
  if (bitfields_.NotifiedOfSubtreeChange())
    return;

  bitfields_.SetNotifiedOfSubtreeChange(true);
  if (Parent())
    Parent()->NotifyAncestorsOfSubtreeChange();
}

void LayoutObject::NotifyOfSubtreeChange() {
  if (!bitfields_.SubtreeChangeListenerRegistered())
    return;
  if (bitfields_.NotifiedOfSubtreeChange())
    return;

  NotifyAncestorsOfSubtreeChange();

  // We can modify the layout tree during layout which means that we may
  // try to schedule this during performLayout. This should no longer
  // happen when crbug.com/370457 is fixed.
  DeprecatedScheduleStyleRecalcDuringLayout marker(GetDocument().Lifecycle());
  GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
}

void LayoutObject::HandleSubtreeModifications() {
  DCHECK(WasNotifiedOfSubtreeChange());
  DCHECK(GetDocument().Lifecycle().StateAllowsLayoutTreeNotifications());

  if (ConsumesSubtreeChangeNotification())
    SubtreeDidChange();

  bitfields_.SetNotifiedOfSubtreeChange(false);

  for (LayoutObject* object = SlowFirstChild(); object;
       object = object->NextSibling()) {
    if (!object->WasNotifiedOfSubtreeChange())
      continue;
    object->HandleSubtreeModifications();
  }
}

LayoutObject* LayoutObject::NextInPreOrder() const {
  if (LayoutObject* o = SlowFirstChild())
    return o;

  return NextInPreOrderAfterChildren();
}

bool LayoutObject::HasClipRelatedProperty() const {
  if (HasClip() || HasOverflowClip() || HasClipPath() ||
      Style()->ContainsPaint())
    return true;
  if (IsBox() && ToLayoutBox(this)->HasControlClip())
    return true;
  return false;
}

LayoutObject* LayoutObject::NextInPreOrderAfterChildren() const {
  LayoutObject* o = NextSibling();
  if (!o) {
    o = Parent();
    while (o && !o->NextSibling())
      o = o->Parent();
    if (o)
      o = o->NextSibling();
  }

  return o;
}

LayoutObject* LayoutObject::NextInPreOrder(
    const LayoutObject* stay_within) const {
  if (LayoutObject* o = SlowFirstChild())
    return o;

  return NextInPreOrderAfterChildren(stay_within);
}

LayoutObject* LayoutObject::NextInPreOrderAfterChildren(
    const LayoutObject* stay_within) const {
  if (this == stay_within)
    return nullptr;

  const LayoutObject* current = this;
  LayoutObject* next = current->NextSibling();
  for (; !next; next = current->NextSibling()) {
    current = current->Parent();
    if (!current || current == stay_within)
      return nullptr;
  }
  return next;
}

LayoutObject* LayoutObject::PreviousInPreOrder() const {
  if (LayoutObject* o = PreviousSibling()) {
    while (LayoutObject* last_child = o->SlowLastChild())
      o = last_child;
    return o;
  }

  return Parent();
}

LayoutObject* LayoutObject::PreviousInPreOrder(
    const LayoutObject* stay_within) const {
  if (this == stay_within)
    return nullptr;

  return PreviousInPreOrder();
}

LayoutObject* LayoutObject::ChildAt(unsigned index) const {
  LayoutObject* child = SlowFirstChild();
  for (unsigned i = 0; child && i < index; i++)
    child = child->NextSibling();
  return child;
}

LayoutObject* LayoutObject::LastLeafChild() const {
  LayoutObject* r = SlowLastChild();
  while (r) {
    LayoutObject* n = nullptr;
    n = r->SlowLastChild();
    if (!n)
      break;
    r = n;
  }
  return r;
}

static void AddLayers(LayoutObject* obj,
                      PaintLayer* parent_layer,
                      LayoutObject*& new_object,
                      PaintLayer*& before_child) {
  if (obj->HasLayer()) {
    if (!before_child && new_object) {
      // We need to figure out the layer that follows newObject. We only do
      // this the first time we find a child layer, and then we update the
      // pointer values for newObject and beforeChild used by everyone else.
      before_child =
          new_object->Parent()->FindNextLayer(parent_layer, new_object);
      new_object = nullptr;
    }
    parent_layer->AddChild(ToLayoutBoxModelObject(obj)->Layer(), before_child);
    return;
  }

  for (LayoutObject* curr = obj->SlowFirstChild(); curr;
       curr = curr->NextSibling())
    AddLayers(curr, parent_layer, new_object, before_child);
}

void LayoutObject::AddLayers(PaintLayer* parent_layer) {
  if (!parent_layer)
    return;

  LayoutObject* object = this;
  PaintLayer* before_child = nullptr;
  blink::AddLayers(this, parent_layer, object, before_child);
}

void LayoutObject::RemoveLayers(PaintLayer* parent_layer) {
  if (!parent_layer)
    return;

  if (HasLayer()) {
    parent_layer->RemoveChild(ToLayoutBoxModelObject(this)->Layer());
    return;
  }

  for (LayoutObject* curr = SlowFirstChild(); curr; curr = curr->NextSibling())
    curr->RemoveLayers(parent_layer);
}

void LayoutObject::MoveLayers(PaintLayer* old_parent, PaintLayer* new_parent) {
  if (!new_parent)
    return;

  if (HasLayer()) {
    PaintLayer* layer = ToLayoutBoxModelObject(this)->Layer();
    DCHECK_EQ(old_parent, layer->Parent());
    if (old_parent)
      old_parent->RemoveChild(layer);
    new_parent->AddChild(layer);
    return;
  }

  for (LayoutObject* curr = SlowFirstChild(); curr; curr = curr->NextSibling())
    curr->MoveLayers(old_parent, new_parent);
}

PaintLayer* LayoutObject::FindNextLayer(PaintLayer* parent_layer,
                                        LayoutObject* start_point,
                                        bool check_parent) {
  // Error check the parent layer passed in. If it's null, we can't find
  // anything.
  if (!parent_layer)
    return 0;

  // Step 1: If our layer is a child of the desired parent, then return our
  // layer.
  PaintLayer* our_layer =
      HasLayer() ? ToLayoutBoxModelObject(this)->Layer() : nullptr;
  if (our_layer && our_layer->Parent() == parent_layer)
    return our_layer;

  // Step 2: If we don't have a layer, or our layer is the desired parent, then
  // descend into our siblings trying to find the next layer whose parent is the
  // desired parent.
  if (!our_layer || our_layer == parent_layer) {
    for (LayoutObject* curr = start_point ? start_point->NextSibling()
                                          : SlowFirstChild();
         curr; curr = curr->NextSibling()) {
      PaintLayer* next_layer =
          curr->FindNextLayer(parent_layer, nullptr, false);
      if (next_layer)
        return next_layer;
    }
  }

  // Step 3: If our layer is the desired parent layer, then we're finished. We
  // didn't find anything.
  if (parent_layer == our_layer)
    return nullptr;

  // Step 4: If |checkParent| is set, climb up to our parent and check its
  // siblings that follow us to see if we can locate a layer.
  if (check_parent && Parent())
    return Parent()->FindNextLayer(parent_layer, this, true);

  return nullptr;
}

PaintLayer* LayoutObject::EnclosingLayer() const {
  for (const LayoutObject* current = this; current;
       current = current->Parent()) {
    if (current->HasLayer())
      return ToLayoutBoxModelObject(current)->Layer();
  }
  // TODO(crbug.com/365897): we should get rid of detached layout subtrees, at
  // which point this code should not be reached.
  return nullptr;
}

PaintLayer* LayoutObject::PaintingLayer() const {
  for (const LayoutObject* current = this; current;
       // Use containingBlock instead of parentCrossingFrames for floating
       // objects to omit any self-painting layers of inline objects that don't
       // paint the floating object.
       current = current->IsFloating() ? current->ContainingBlock()
                                       : current->ParentCrossingFrames()) {
    if (current->HasLayer() &&
        ToLayoutBoxModelObject(current)->Layer()->IsSelfPaintingLayer()) {
      return ToLayoutBoxModelObject(current)->Layer();
    } else if (current->IsColumnSpanAll()) {
      // Column spanners paint through their multicolumn containers which can
      // be accessed through the associated out-of-flow placeholder's parent.
      current = current->SpannerPlaceholder();
    }
  }
  // TODO(crbug.com/365897): we should get rid of detached layout subtrees, at
  // which point this code should not be reached.
  return nullptr;
}

bool LayoutObject::ScrollRectToVisible(const LayoutRect& rect,
                                       const ScrollAlignment& align_x,
                                       const ScrollAlignment& align_y,
                                       ScrollType scroll_type,
                                       bool make_visible_in_visual_viewport) {
  LayoutBox* enclosing_box = this->EnclosingBox();
  if (!enclosing_box)
    return false;

  enclosing_box->ScrollRectToVisible(rect, align_x, align_y, scroll_type,
                                     make_visible_in_visual_viewport);
  return true;
}

LayoutBox* LayoutObject::EnclosingBox() const {
  LayoutObject* curr = const_cast<LayoutObject*>(this);
  while (curr) {
    if (curr->IsBox())
      return ToLayoutBox(curr);
    curr = curr->Parent();
  }

  NOTREACHED();
  return nullptr;
}

LayoutBoxModelObject* LayoutObject::EnclosingBoxModelObject() const {
  LayoutObject* curr = const_cast<LayoutObject*>(this);
  while (curr) {
    if (curr->IsBoxModelObject())
      return ToLayoutBoxModelObject(curr);
    curr = curr->Parent();
  }

  NOTREACHED();
  return nullptr;
}

LayoutBox* LayoutObject::EnclosingScrollableBox() const {
  for (LayoutObject* ancestor = Parent(); ancestor;
       ancestor = ancestor->Parent()) {
    if (!ancestor->IsBox())
      continue;

    LayoutBox* ancestor_box = ToLayoutBox(ancestor);
    if (ancestor_box->CanBeScrolledAndHasScrollableArea())
      return ancestor_box;
  }

  return nullptr;
}

LayoutFlowThread* LayoutObject::LocateFlowThreadContainingBlock() const {
  DCHECK(IsInsideFlowThread());

  // See if we have the thread cached because we're in the middle of layout.
  if (LayoutState* layout_state = View()->GetLayoutState()) {
    // TODO(mstensho): We should really just return whatever
    // layoutState->flowThread() returns here, also if the value is nullptr.
    if (LayoutFlowThread* flow_thread = layout_state->FlowThread())
      return flow_thread;
  }

  // Not in the middle of layout so have to find the thread the slow way.
  return LayoutFlowThread::LocateFlowThreadContainingBlockOf(
      *this, LayoutFlowThread::kAnyAncestor);
}

static inline bool ObjectIsRelayoutBoundary(const LayoutObject* object) {
  // FIXME: In future it may be possible to broaden these conditions in order to
  // improve performance.
  if (object->IsTextControl())
    return true;

  if (object->IsSVGRoot())
    return true;

  // LayoutInline can't be relayout roots since LayoutBlockFlow is responsible
  // for layouting them.
  if (object->IsLayoutInline())
    return false;

  // Table parts can't be relayout roots since the table is responsible for
  // layouting all the parts.
  if (object->IsTablePart())
    return false;

  const ComputedStyle* style = object->Style();
  if (style->ContainsLayout() && style->ContainsSize())
    return true;

  if (!object->HasOverflowClip())
    return false;

  // If either dimension is percent-based, intrinsic, or anything but fixed,
  // this object cannot form a re-layout boundary. A non-fixed computed logical
  // height will allow the object to grow and shrink based on the content
  // inside. The same goes for for logical width, if this objects is inside a
  // shrink-to-fit container, for instance.
  if (!style->Width().IsFixed() || !style->Height().IsFixed())
    return false;

  // Scrollbar parts can be removed during layout. Avoid the complexity of
  // having to deal with that.
  if (object->IsLayoutScrollbarPart())
    return false;

  // In general we can't relayout a flex item independently of its container;
  // not only is the result incorrect due to the override size that's set, it
  // also messes with the cached main size on the flexbox.
  if (object->IsBox() && ToLayoutBox(object)->IsFlexItem())
    return false;

  // Inside multicol it's generally problematic to allow relayout roots. The
  // multicol container itself may be scheduled for relayout as well (due to
  // other changes that may have happened since the previous layout pass),
  // which might affect the column heights, which may affect how this object
  // breaks across columns). Spanners may also have been added or removed since
  // the previous layout pass, which is just another way of affecting the column
  // heights (and the number of rows). Instead of identifying cases where it's
  // safe to allow relayout roots, just disallow them inside multicol.
  if (object->IsInsideFlowThread())
    return false;

  return true;
}

void LayoutObject::MarkContainerChainForLayout(bool schedule_relayout,
                                               SubtreeLayoutScope* layouter) {
#if DCHECK_IS_ON()
  DCHECK(!IsSetNeedsLayoutForbidden());
#endif
  DCHECK(!layouter || this != layouter->Root());
  // When we're in layout, we're marking a descendant as needing layout with
  // the intention of visiting it during this layout. We shouldn't be
  // scheduling it to be laid out later. Also, scheduleRelayout() must not be
  // called while iterating FrameView::m_layoutSubtreeRootList.
  schedule_relayout &= !GetFrameView()->IsInPerformLayout();

  LayoutObject* object = Container();
  LayoutObject* last = this;

  bool simplified_normal_flow_layout = NeedsSimplifiedNormalFlowLayout() &&
                                       !SelfNeedsLayout() &&
                                       !NormalChildNeedsLayout();

  while (object) {
    if (object->SelfNeedsLayout())
      return;

    // Don't mark the outermost object of an unrooted subtree. That object will
    // be marked when the subtree is added to the document.
    LayoutObject* container = object->Container();
    if (!container && !object->IsLayoutView())
      return;
    if (!last->IsTextOrSVGChild() && last->Style()->HasOutOfFlowPosition()) {
      object = last->ContainingBlock();
      if (object->PosChildNeedsLayout())
        return;
      container = object->Container();
      object->SetPosChildNeedsLayout(true);
      simplified_normal_flow_layout = true;
    } else if (simplified_normal_flow_layout) {
      if (object->NeedsSimplifiedNormalFlowLayout())
        return;
      object->SetNeedsSimplifiedNormalFlowLayout(true);
    } else {
      if (object->NormalChildNeedsLayout())
        return;
      object->SetNormalChildNeedsLayout(true);
    }
#if DCHECK_IS_ON()
    DCHECK(!object->IsSetNeedsLayoutForbidden());
#endif

    if (layouter) {
      layouter->RecordObjectMarkedForLayout(object);
      if (object == layouter->Root())
        return;
    }

    last = object;
    if (schedule_relayout && ObjectIsRelayoutBoundary(last))
      break;
    object = container;
  }

  if (schedule_relayout)
    last->ScheduleRelayout();
}

#if DCHECK_IS_ON()
void LayoutObject::CheckBlockPositionedObjectsNeedLayout() {
  DCHECK(!NeedsLayout());

  if (IsLayoutBlock())
    ToLayoutBlock(this)->CheckPositionedObjectsNeedLayout();
}
#endif

void LayoutObject::SetPreferredLogicalWidthsDirty(
    MarkingBehavior mark_parents) {
  bitfields_.SetPreferredLogicalWidthsDirty(true);
  if (mark_parents == kMarkContainerChain &&
      (IsText() || !Style()->HasOutOfFlowPosition()))
    InvalidateContainerPreferredLogicalWidths();
}

void LayoutObject::ClearPreferredLogicalWidthsDirty() {
  bitfields_.SetPreferredLogicalWidthsDirty(false);
}

inline void LayoutObject::InvalidateContainerPreferredLogicalWidths() {
  // In order to avoid pathological behavior when inlines are deeply nested, we
  // do include them in the chain that we mark dirty (even though they're kind
  // of irrelevant).
  LayoutObject* o = IsTableCell() ? ContainingBlock() : Container();
  while (o && !o->PreferredLogicalWidthsDirty()) {
    // Don't invalidate the outermost object of an unrooted subtree. That object
    // will be invalidated when the subtree is added to the document.
    LayoutObject* container =
        o->IsTableCell() ? o->ContainingBlock() : o->Container();
    if (!container && !o->IsLayoutView())
      break;

    o->bitfields_.SetPreferredLogicalWidthsDirty(true);
    // A positioned object has no effect on the min/max width of its containing
    // block ever. We can optimize this case and not go up any further.
    if (o->Style()->HasOutOfFlowPosition())
      break;
    o = container;
  }
}

LayoutObject* LayoutObject::ContainerForAbsolutePosition(
    AncestorSkipInfo* skip_info) const {
  // We technically just want our containing block, but we may not have one if
  // we're part of an uninstalled subtree. We'll climb as high as we can though.
  for (LayoutObject* object = Parent(); object; object = object->Parent()) {
    if (object->CanContainAbsolutePositionObjects())
      return object;
    if (skip_info)
      skip_info->Update(*object);
  }
  return nullptr;
}

LayoutBlock* LayoutObject::ContainerForFixedPosition(
    AncestorSkipInfo* skip_info) const {
  DCHECK(!IsText());

  LayoutObject* object = Parent();
  for (; object && !object->CanContainFixedPositionObjects();
       object = object->Parent()) {
    if (skip_info)
      skip_info->Update(*object);
  }

  DCHECK(!object || !object->IsAnonymousBlock());
  return ToLayoutBlock(object);
}

LayoutBlock* LayoutObject::ContainingBlockForAbsolutePosition(
    AncestorSkipInfo* skip_info) const {
  LayoutObject* object = ContainerForAbsolutePosition(skip_info);

  // For relpositioned inlines, we return the nearest non-anonymous enclosing
  // block. We don't try to return the inline itself. This allows us to avoid
  // having a positioned objects list in all LayoutInlines and lets us return a
  // strongly-typed LayoutBlock* result from this method. The container() method
  // can actually be used to obtain the inline directly.
  if (object && object->IsInline() && !object->IsAtomicInlineLevel()) {
    DCHECK(object->Style()->HasInFlowPosition());
    object = object->ContainingBlock(skip_info);
  }

  if (object && !object->IsLayoutBlock())
    object = object->ContainingBlock(skip_info);

  while (object && object->IsAnonymousBlock())
    object = object->ContainingBlock(skip_info);

  if (!object || !object->IsLayoutBlock())
    return nullptr;  // This can still happen in case of an orphaned tree

  return ToLayoutBlock(object);
}

LayoutBlock* LayoutObject::ContainingBlock(AncestorSkipInfo* skip_info) const {
  LayoutObject* object = Parent();
  if (!object && IsLayoutScrollbarPart())
    object = ToLayoutScrollbarPart(this)->ScrollbarStyleSource();
  if (!IsTextOrSVGChild()) {
    if (style_->GetPosition() == EPosition::kFixed)
      return ContainerForFixedPosition(skip_info);
    if (style_->GetPosition() == EPosition::kAbsolute)
      return ContainingBlockForAbsolutePosition(skip_info);
  }
  if (IsColumnSpanAll()) {
    object = SpannerPlaceholder()->ContainingBlock();
  } else {
    while (object && ((object->IsInline() && !object->IsAtomicInlineLevel()) ||
                      !object->IsLayoutBlock())) {
      if (skip_info)
        skip_info->Update(*object);
      object = object->Parent();
    }
  }

  if (!object || !object->IsLayoutBlock())
    return nullptr;  // This can still happen in case of an orphaned tree

  return ToLayoutBlock(object);
}

FloatRect LayoutObject::AbsoluteBoundingBoxFloatRect() const {
  Vector<FloatQuad> quads;
  AbsoluteQuads(quads);

  size_t n = quads.size();
  if (n == 0)
    return FloatRect();

  FloatRect result = quads[0].BoundingBox();
  for (size_t i = 1; i < n; ++i)
    result.Unite(quads[i].BoundingBox());
  return result;
}

IntRect LayoutObject::AbsoluteBoundingBoxRect(MapCoordinatesFlags flags) const {
  Vector<FloatQuad> quads;
  AbsoluteQuads(quads, flags);

  size_t n = quads.size();
  if (!n)
    return IntRect();

  IntRect result = quads[0].EnclosingBoundingBox();
  for (size_t i = 1; i < n; ++i)
    result.Unite(quads[i].EnclosingBoundingBox());
  return result;
}

IntRect LayoutObject::AbsoluteBoundingBoxRectIgnoringTransforms() const {
  FloatPoint abs_pos = LocalToAbsolute();
  Vector<IntRect> rects;
  AbsoluteRects(rects, FlooredLayoutPoint(abs_pos));

  size_t n = rects.size();
  if (!n)
    return IntRect();

  IntRect result = rects[0];
  for (size_t i = 1; i < n; ++i)
    result.Unite(rects[i]);
  return result;
}

IntRect LayoutObject::AbsoluteElementBoundingBoxRect() const {
  Vector<LayoutRect> rects;
  const LayoutBoxModelObject& container = EnclosingLayer()->GetLayoutObject();
  AddElementVisualOverflowRects(
      rects, LayoutPoint(LocalToAncestorPoint(FloatPoint(), &container)));
  return container.LocalToAbsoluteQuad(FloatQuad(FloatRect(UnionRect(rects))))
      .EnclosingBoundingBox();
}

FloatRect LayoutObject::AbsoluteBoundingBoxRectForRange(const Range* range) {
  if (!range || !range->startContainer())
    return FloatRect();

  range->OwnerDocument().UpdateStyleAndLayout();

  Vector<FloatQuad> quads;
  quads.AppendVector(ComputeTextQuads(EphemeralRange(range)));

  FloatRect result;
  for (size_t i = 0; i < quads.size(); ++i)
    result.Unite(quads[i].BoundingBox());

  return result;
}

void LayoutObject::AddAbsoluteRectForLayer(IntRect& result) {
  if (HasLayer())
    result.Unite(AbsoluteBoundingBoxRect());
  for (LayoutObject* current = SlowFirstChild(); current;
       current = current->NextSibling())
    current->AddAbsoluteRectForLayer(result);
}

IntRect LayoutObject::AbsoluteBoundingBoxRectIncludingDescendants() const {
  IntRect result = AbsoluteBoundingBoxRect();
  for (LayoutObject* current = SlowFirstChild(); current;
       current = current->NextSibling())
    current->AddAbsoluteRectForLayer(result);
  return result;
}

void LayoutObject::Paint(const PaintInfo&, const LayoutPoint&) const {}

const LayoutBoxModelObject& LayoutObject::ContainerForPaintInvalidation()
    const {
  CHECK(IsRooted());

  if (const LayoutBoxModelObject* paint_invalidation_container =
          EnclosingCompositedContainer())
    return *paint_invalidation_container;

  // If the current frame is not composited, we send just return the main
  // frame's LayoutView so that we generate invalidations on the window.
  const LayoutView* layout_view = View();
  while (const LayoutObject* owner_object =
             LayoutAPIShim::ConstLayoutObjectFrom(
                 layout_view->GetFrame()->OwnerLayoutItem()))
    layout_view = owner_object->View();

  DCHECK(layout_view);
  return *layout_view;
}

const LayoutBoxModelObject* LayoutObject::EnclosingCompositedContainer() const {
  LayoutBoxModelObject* container = nullptr;
  // FIXME: CompositingState is not necessarily up to date for many callers of
  // this function.
  DisableCompositingQueryAsserts disabler;

  if (PaintLayer* painting_layer = this->PaintingLayer()) {
    if (PaintLayer* compositing_layer =
            painting_layer
                ->EnclosingLayerForPaintInvalidationCrossingFrameBoundaries())
      container = &compositing_layer->GetLayoutObject();
  }
  return container;
}

String LayoutObject::DecoratedName() const {
  StringBuilder name;
  name.Append(this->GetName());

  if (IsAnonymous())
    name.Append(" (anonymous)");
  // FIXME: Remove the special case for LayoutView here (requires rebaseline of
  // all tests).
  if (IsOutOfFlowPositioned() && !IsLayoutView())
    name.Append(" (positioned)");
  if (IsRelPositioned())
    name.Append(" (relative positioned)");
  if (IsStickyPositioned())
    name.Append(" (sticky positioned)");
  if (IsFloating())
    name.Append(" (floating)");
  if (SpannerPlaceholder())
    name.Append(" (column spanner)");

  return name.ToString();
}

String LayoutObject::DebugName() const {
  StringBuilder name;
  name.Append(DecoratedName());

  if (const Node* node = this->GetNode()) {
    name.Append(' ');
    name.Append(node->DebugName());
  }
  return name.ToString();
}

bool LayoutObject::IsPaintInvalidationContainer() const {
  return HasLayer() &&
         ToLayoutBoxModelObject(this)->Layer()->IsPaintInvalidationContainer();
}

void LayoutObject::InvalidateDisplayItemClients(
    PaintInvalidationReason reason) const {
  // This default implementation invalidates only the object itself as a
  // DisplayItemClient.
  ObjectPaintInvalidator(*this).InvalidateDisplayItemClient(*this, reason);
}

bool LayoutObject::CompositedScrollsWithRespectTo(
    const LayoutBoxModelObject& paint_invalidation_container) const {
  return paint_invalidation_container.UsesCompositedScrolling() &&
         this != &paint_invalidation_container;
}

IntSize LayoutObject::ScrollAdjustmentForPaintInvalidation(
    const LayoutBoxModelObject& paint_invalidation_container) const {
  // Non-composited scrolling should be included in the bounds of scrolled
  // items. Since mapToVisualRectInAncestorSpace does not include scrolling of
  // the ancestor, we need to add it back in after.
  if (paint_invalidation_container.IsBox() &&
      !paint_invalidation_container.UsesCompositedScrolling() &&
      this != &paint_invalidation_container) {
    const LayoutBox* box = ToLayoutBox(&paint_invalidation_container);
    if (box->HasOverflowClip())
      return -box->ScrolledContentOffset();
  }
  return IntSize();
}

LayoutRect LayoutObject::InvalidatePaintRectangle(
    const LayoutRect& dirty_rect,
    DisplayItemClient* display_item_client) const {
  return ObjectPaintInvalidator(*this).InvalidatePaintRectangle(
      dirty_rect, display_item_client);
}

void LayoutObject::DeprecatedInvalidateTree(
    const PaintInvalidationState& paint_invalidation_state) {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled());
  EnsureIsReadyForPaintInvalidation();

  // If we didn't need paint invalidation then our children don't need as well.
  // Skip walking down the tree as everything should be fine below us.
  if (!ShouldCheckForPaintInvalidationWithPaintInvalidationState(
          paint_invalidation_state))
    return;

  PaintInvalidationState new_paint_invalidation_state(paint_invalidation_state,
                                                      *this);

  if (MayNeedPaintInvalidationSubtree()) {
    new_paint_invalidation_state
        .SetForceSubtreeInvalidationCheckingWithinContainer();
  }

  PaintInvalidationReason reason =
      DeprecatedInvalidatePaint(new_paint_invalidation_state);
  new_paint_invalidation_state.UpdateForChildren(reason);
  DeprecatedInvalidatePaintOfSubtrees(new_paint_invalidation_state);

  ClearPaintInvalidationFlags();
}

DISABLE_CFI_PERF
void LayoutObject::DeprecatedInvalidatePaintOfSubtrees(
    const PaintInvalidationState& child_paint_invalidation_state) {
  DCHECK(!RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled());

  for (auto* child = SlowFirstChild(); child; child = child->NextSibling())
    child->DeprecatedInvalidateTree(child_paint_invalidation_state);
}

LayoutRect LayoutObject::SelectionRectInViewCoordinates() const {
  LayoutRect selection_rect = LocalSelectionRect();
  if (!selection_rect.IsEmpty())
    MapToVisualRectInAncestorSpace(View(), selection_rect);
  return selection_rect;
}

PaintInvalidationReason LayoutObject::DeprecatedInvalidatePaint(
    const PaintInvalidationState& paint_invalidation_state) {
  DCHECK_EQ(&paint_invalidation_state.CurrentObject(), this);

  if (StyleRef().HasOutline()) {
    PaintLayer& layer = paint_invalidation_state.PaintingLayer();
    if (&layer.GetLayoutObject() != this)
      layer.SetNeedsPaintPhaseDescendantOutlines();
  }

  LayoutView* v = View();
  if (v->GetDocument().Printing())
    return PaintInvalidationReason::kNone;  // Don't invalidate paints if we're
                                            // printing.

  PaintInvalidatorContextAdapter context(paint_invalidation_state);

  const LayoutBoxModelObject& paint_invalidation_container =
      paint_invalidation_state.PaintInvalidationContainer();
  DCHECK(paint_invalidation_container == ContainerForPaintInvalidation());

  ObjectPaintInvalidator paint_invalidator(*this);
  context.old_visual_rect = VisualRect();
  context.old_location = paint_invalidator.LocationInBacking();
  LayoutRect new_visual_rect =
      paint_invalidation_state.ComputeVisualRectInBacking();
  context.new_location = paint_invalidation_state.ComputeLocationInBacking(
      new_visual_rect.Location());

  SetVisualRect(new_visual_rect);
  paint_invalidator.SetLocationInBacking(context.new_location);

  if (!ShouldCheckForPaintInvalidation() &&
      paint_invalidation_state
          .ForcedSubtreeInvalidationRectUpdateWithinContainerOnly()) {
    // We are done updating the visual rect. No other paint invalidation work
    // to do for this object.
    return PaintInvalidationReason::kNone;
  }

  return InvalidatePaint(context);
}

DISABLE_CFI_PERF
PaintInvalidationReason LayoutObject::InvalidatePaint(
    const PaintInvalidatorContext& context) const {
  return ObjectPaintInvalidatorWithContext(*this, context).InvalidatePaint();
}

void LayoutObject::AdjustVisualRectForCompositedScrolling(
    LayoutRect& rect,
    const LayoutBoxModelObject& paint_invalidation_container) const {
  if (CompositedScrollsWithRespectTo(paint_invalidation_container)) {
    LayoutSize offset(
        -ToLayoutBox(&paint_invalidation_container)->ScrolledContentOffset());
    rect.Move(offset);
  }
}

LayoutRect LayoutObject::VisualRectIncludingCompositedScrolling(
    const LayoutBoxModelObject& paint_invalidation_container) const {
  LayoutRect rect = VisualRect();
  AdjustVisualRectForCompositedScrolling(rect, paint_invalidation_container);
  return rect;
}

void LayoutObject::ClearPreviousVisualRects() {
  SetVisualRect(LayoutRect());
  ObjectPaintInvalidator(*this).SetLocationInBacking(LayoutPoint());
  // Ensure check paint invalidation of subtree that would be triggered by
  // location change if we had valid previous location.
  SetMayNeedPaintInvalidationSubtree();
  // After clearing ("invalidating") the visual rects, mark this object as
  // needing to re-compute them.
  SetShouldDoFullPaintInvalidation();
}

LayoutRect LayoutObject::AbsoluteVisualRect() const {
  LayoutRect rect = LocalVisualRect();
  MapToVisualRectInAncestorSpace(View(), rect);
  return rect;
}

LayoutRect LayoutObject::LocalVisualRect() const {
  NOTREACHED();
  return LayoutRect();
}

bool LayoutObject::MapToVisualRectInAncestorSpace(
    const LayoutBoxModelObject* ancestor,
    LayoutRect& rect,
    VisualRectFlags visual_rect_flags) const {
  TransformState transform_state(TransformState::kApplyTransformDirection,
                                 FloatQuad(FloatRect(rect)));
  bool retval = MapToVisualRectInAncestorSpaceInternal(
      ancestor, transform_state, visual_rect_flags);
  transform_state.Flatten();
  rect = LayoutRect(transform_state.LastPlanarQuad().BoundingBox());
  return retval;
}

bool LayoutObject::MapToVisualRectInAncestorSpaceInternal(
    const LayoutBoxModelObject* ancestor,
    TransformState& transform_state,
    VisualRectFlags visual_rect_flags) const {
  // For any layout object that doesn't override this method (the main example
  // is LayoutText), the rect is assumed to be in the parent's coordinate space,
  // except for container flip.

  if (ancestor == this)
    return true;

  if (LayoutObject* parent = this->Parent()) {
    if (parent->IsBox()) {
      LayoutBox* parent_box = ToLayoutBox(parent);

      // Never flip for SVG as it handles writing modes itself.
      if (!IsSVG()) {
        transform_state.Flatten();
        LayoutRect rect(transform_state.LastPlanarQuad().BoundingBox());
        parent_box->FlipForWritingMode(rect);
        transform_state.SetQuad(FloatQuad(FloatRect(rect)));
      }

      bool preserve3d = parent->Style()->Preserves3D() && !parent->IsText();

      TransformState::TransformAccumulation accumulation =
          preserve3d ? TransformState::kAccumulateTransform
                     : TransformState::kFlattenTransform;

      if (parent != ancestor &&
          !parent_box->MapScrollingContentsRectToBoxSpace(
              transform_state, accumulation, visual_rect_flags))
        return false;
    }
    return parent->MapToVisualRectInAncestorSpaceInternal(
        ancestor, transform_state, visual_rect_flags);
  }
  return true;
}

void LayoutObject::DirtyLinesFromChangedChild(LayoutObject*, MarkingBehavior) {}

#ifndef NDEBUG

void LayoutObject::ShowTreeForThis() const {
  if (GetNode())
    ::showTree(GetNode());
}

void LayoutObject::ShowLayoutTreeForThis() const {
  showLayoutTree(this, 0);
}

void LayoutObject::ShowLineTreeForThis() const {
  if (LayoutBlock* cb = ContainingBlock()) {
    if (cb->IsLayoutBlockFlow())
      ToLayoutBlockFlow(cb)->ShowLineTreeAndMark(nullptr, nullptr, nullptr,
                                                 nullptr, this);
  }
}

void LayoutObject::ShowLayoutObject() const {
  StringBuilder string_builder;
  ShowLayoutObject(string_builder);
}

void LayoutObject::ShowLayoutObject(StringBuilder& string_builder) const {
  string_builder.Append(
      String::Format("%s %p", DecoratedName().Ascii().data(), this));

  if (IsText() && ToLayoutText(this)->IsTextFragment())
    string_builder.Append(String::Format(
        " \"%s\" ", ToLayoutText(this)->GetText().Ascii().data()));

  if (VirtualContinuation())
    string_builder.Append(
        String::Format(" continuation=%p", VirtualContinuation()));

  if (GetNode()) {
    while (string_builder.length() < kShowTreeCharacterOffset)
      string_builder.Append(' ');
    string_builder.Append('\t');
    WTFLogAlways("%s%s", string_builder.ToString().Utf8().data(),
                 GetNode()->ToString().Utf8().data());
  } else {
    WTFLogAlways("%s", string_builder.ToString().Utf8().data());
  }
}

void LayoutObject::ShowLayoutTreeAndMark(const LayoutObject* marked_object1,
                                         const char* marked_label1,
                                         const LayoutObject* marked_object2,
                                         const char* marked_label2,
                                         unsigned depth) const {
  StringBuilder string_builder;
  if (marked_object1 == this && marked_label1)
    string_builder.Append(marked_label1);
  if (marked_object2 == this && marked_label2)
    string_builder.Append(marked_label2);
  while (string_builder.length() < depth * 2)
    string_builder.Append(' ');

  ShowLayoutObject(string_builder);

  for (const LayoutObject* child = SlowFirstChild(); child;
       child = child->NextSibling())
    child->ShowLayoutTreeAndMark(marked_object1, marked_label1, marked_object2,
                                 marked_label2, depth + 1);
}

#endif  // NDEBUG

bool LayoutObject::IsSelectable() const {
  return !IsInert() && !(Style()->UserSelect() == EUserSelect::kNone &&
                         Style()->UserModify() == EUserModify::kReadOnly);
}

Color LayoutObject::SelectionBackgroundColor() const {
  if (!IsSelectable())
    return Color::kTransparent;

  if (RefPtr<ComputedStyle> pseudo_style = GetUncachedSelectionStyle())
    return ResolveColor(*pseudo_style, CSSPropertyBackgroundColor)
        .BlendWithWhite();
  return GetFrame()->Selection().FrameIsFocusedAndActive()
             ? LayoutTheme::GetTheme().ActiveSelectionBackgroundColor()
             : LayoutTheme::GetTheme().InactiveSelectionBackgroundColor();
}

Color LayoutObject::SelectionColor(
    int color_property,
    const GlobalPaintFlags global_paint_flags) const {
  // If the element is unselectable, or we are only painting the selection,
  // don't override the foreground color with the selection foreground color.
  if (!IsSelectable() || (global_paint_flags & kGlobalPaintSelectionOnly))
    return ResolveColor(color_property);

  if (RefPtr<ComputedStyle> pseudo_style = GetUncachedSelectionStyle())
    return ResolveColor(*pseudo_style, color_property);
  if (!LayoutTheme::GetTheme().SupportsSelectionForegroundColors())
    return ResolveColor(color_property);
  return GetFrame()->Selection().FrameIsFocusedAndActive()
             ? LayoutTheme::GetTheme().ActiveSelectionForegroundColor()
             : LayoutTheme::GetTheme().InactiveSelectionForegroundColor();
}

Color LayoutObject::SelectionForegroundColor(
    const GlobalPaintFlags global_paint_flags) const {
  return SelectionColor(CSSPropertyWebkitTextFillColor, global_paint_flags);
}

Color LayoutObject::SelectionEmphasisMarkColor(
    const GlobalPaintFlags global_paint_flags) const {
  return SelectionColor(CSSPropertyWebkitTextEmphasisColor, global_paint_flags);
}

std::pair<int, int> LayoutObject::SelectionStartEnd() const {
  DCHECK(!View()->NeedsLayout());
  return GetFrame()->Selection().LayoutSelectionStartEnd();
}

// Called when an object that was floating or positioned becomes a normal flow
// object again. We have to make sure the layout tree updates as needed to
// accommodate the new normal flow object.
static inline void HandleDynamicFloatPositionChange(LayoutObject* object) {
  // We have gone from not affecting the inline status of the parent flow to
  // suddenly having an impact.  See if there is a mismatch between the parent
  // flow's childrenInline() state and our state.
  object->SetInline(object->Style()->IsDisplayInlineType());
  if (object->IsInline() != object->Parent()->ChildrenInline()) {
    if (!object->IsInline()) {
      ToLayoutBoxModelObject(object->Parent())->ChildBecameNonInline(object);
    } else {
      // An anonymous block must be made to wrap this inline.
      LayoutBlock* block =
          ToLayoutBlock(object->Parent())->CreateAnonymousBlock();
      LayoutObjectChildList* childlist = object->Parent()->VirtualChildren();
      childlist->InsertChildNode(object->Parent(), block, object);
      block->Children()->AppendChildNode(
          block, childlist->RemoveChildNode(object->Parent(), object));
    }
  }
}

StyleDifference LayoutObject::AdjustStyleDifference(
    StyleDifference diff) const {
  if (diff.TransformChanged() && IsSVG()) {
    // Skip a full layout for transforms at the html/svg boundary which do not
    // affect sizes inside SVG.
    if (!IsSVGRoot())
      diff.SetNeedsFullLayout();
  }

  if (!RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    // If transform changed, and the layer does not paint into its own separate
    // backing, then we need to invalidate paints.
    if (diff.TransformChanged()) {
      // Text nodes share style with their parents but transforms don't apply to
      // them, hence the !isText() check.
      if (!IsText() &&
          (!HasLayer() || !ToLayoutBoxModelObject(this)
                               ->Layer()
                               ->HasStyleDeterminedDirectCompositingReasons()))
        diff.SetNeedsPaintInvalidationSubtree();
    }

    // If opacity or zIndex changed, and the layer does not paint into its own
    // separate backing, then we need to invalidate paints (also
    // ignoring text nodes).
    if (diff.OpacityChanged() || diff.ZIndexChanged()) {
      if (!IsText() &&
          (!HasLayer() || !ToLayoutBoxModelObject(this)
                               ->Layer()
                               ->HasStyleDeterminedDirectCompositingReasons()))
        diff.SetNeedsPaintInvalidationSubtree();
    }

    // If filter changed, and the layer does not paint into its own separate
    // backing or it paints with filters, then we need to invalidate paints.
    if (diff.FilterChanged() && HasLayer()) {
      PaintLayer* layer = ToLayoutBoxModelObject(this)->Layer();
      if (!layer->HasStyleDeterminedDirectCompositingReasons() ||
          layer->PaintsWithFilters())
        diff.SetNeedsPaintInvalidationSubtree();
    }

    // If backdrop filter changed, and the layer does not paint into its own
    // separate backing or it paints with filters, then we need to invalidate
    // paints.
    if (diff.BackdropFilterChanged() && HasLayer()) {
      PaintLayer* layer = ToLayoutBoxModelObject(this)->Layer();
      if (!layer->HasStyleDeterminedDirectCompositingReasons() ||
          layer->PaintsWithBackdropFilters())
        diff.SetNeedsPaintInvalidationSubtree();
    }
  }

  // TODO(wangxianzhu): We may avoid subtree paint invalidation on CSS clip
  // change for SPv2.
  if (diff.CssClipChanged())
    diff.SetNeedsPaintInvalidationSubtree();

  // Optimization: for decoration/color property changes, invalidation is only
  // needed if we have style or text affected by these properties.
  if (diff.TextDecorationOrColorChanged() &&
      !diff.NeedsFullPaintInvalidation()) {
    if (Style()->HasBorderColorReferencingCurrentColor() ||
        Style()->HasOutlineWithCurrentColor() ||
        Style()->HasBackgroundRelatedColorReferencingCurrentColor() ||
        // Skip any text nodes that do not contain text boxes. Whitespace cannot
        // be skipped or we will miss invalidating decorations (e.g.,
        // underlines).
        (IsText() && !IsBR() && ToLayoutText(this)->HasTextBoxes()) ||
        (IsSVG() && Style()->SvgStyle().IsFillColorCurrentColor()) ||
        (IsSVG() && Style()->SvgStyle().IsStrokeColorCurrentColor()) ||
        IsListMarker())
      diff.SetNeedsPaintInvalidationObject();
  }

  // The answer to layerTypeRequired() for plugins, iframes, and canvas can
  // change without the actual style changing, since it depends on whether we
  // decide to composite these elements. When the/ layer status of one of these
  // elements changes, we need to force a layout.
  if (!diff.NeedsFullLayout() && Style() && IsBoxModelObject()) {
    bool requires_layer =
        ToLayoutBoxModelObject(this)->LayerTypeRequired() != kNoPaintLayer;
    if (HasLayer() != requires_layer)
      diff.SetNeedsFullLayout();
  }

  return diff;
}

void LayoutObject::SetPseudoStyle(PassRefPtr<ComputedStyle> pseudo_style) {
  DCHECK(pseudo_style->StyleType() == kPseudoIdBefore ||
         pseudo_style->StyleType() == kPseudoIdAfter ||
         pseudo_style->StyleType() == kPseudoIdFirstLetter);

  // FIXME: We should consider just making all pseudo items use an inherited
  // style.

  // Images are special and must inherit the pseudoStyle so the width and height
  // of the pseudo element doesn't change the size of the image. In all other
  // cases we can just share the style.
  //
  // Quotes are also LayoutInline, so we need to create an inherited style to
  // avoid getting an inline with positioning or an invalid display.
  //
  if (IsImage() || IsQuote()) {
    RefPtr<ComputedStyle> style = ComputedStyle::Create();
    style->InheritFrom(*pseudo_style);
    SetStyle(std::move(style));
    return;
  }

  SetStyle(std::move(pseudo_style));
}

void LayoutObject::FirstLineStyleDidChange(const ComputedStyle& old_style,
                                           const ComputedStyle& new_style) {
  StyleDifference diff = old_style.VisualInvalidationDiff(new_style);

  if (diff.NeedsFullPaintInvalidation() ||
      diff.TextDecorationOrColorChanged()) {
    // We need to invalidate all inline boxes in the first line, because they
    // need to be repainted with the new style, e.g. background, font style,
    // etc.
    LayoutBlockFlow* first_line_container = nullptr;
    if (BehavesLikeBlockContainer()) {
      // This object is a LayoutBlock having PseudoIdFirstLine pseudo style
      // changed.
      first_line_container =
          ToLayoutBlock(this)->NearestInnerBlockWithFirstLine();
    } else if (IsLayoutInline()) {
      // This object is a LayoutInline having FIRST_LINE_INHERITED pesudo style
      // changed. This method can be called even if the LayoutInline doesn't
      // intersect the first line, but we only need to invalidate if it does.
      if (InlineBox* first_line_box =
              ToLayoutInline(this)->FirstLineBoxIncludingCulling()) {
        if (first_line_box->IsFirstLineStyle())
          first_line_container = ToLayoutBlockFlow(ContainingBlock());
      }
    }
    if (first_line_container)
      first_line_container->SetShouldDoFullPaintInvalidationForFirstLine();
  }
  if (diff.NeedsLayout())
    SetNeedsLayoutAndPrefWidthsRecalc(LayoutInvalidationReason::kStyleChange);
}

void LayoutObject::MarkAncestorsForOverflowRecalcIfNeeded() {
  LayoutObject* object = this;
  do {
    // Cell and row need to propagate the flag to their containing section and
    // row as their containing block is the table wrapper.
    // This enables us to only recompute overflow the modified sections / rows.
    object = object->IsTableCell() || object->IsTableRow()
                 ? object->Parent()
                 : object->ContainingBlock();
    if (object)
      object->SetChildNeedsOverflowRecalcAfterStyleChange();
  } while (object);
}

void LayoutObject::SetNeedsOverflowRecalcAfterStyleChange() {
  bool needed_recalc = NeedsOverflowRecalcAfterStyleChange();
  SetSelfNeedsOverflowRecalcAfterStyleChange();
  SetMayNeedPaintInvalidation();
  if (!needed_recalc)
    MarkAncestorsForOverflowRecalcIfNeeded();
}

DISABLE_CFI_PERF
void LayoutObject::SetStyle(PassRefPtr<ComputedStyle> style) {
  DCHECK(style);

  if (style_ == style) {
    // We need to run through adjustStyleDifference() for iframes, plugins, and
    // canvas so style sharing is disabled for them. That should ensure that we
    // never hit this code path.
    DCHECK(!IsLayoutIFrame());
    DCHECK(!IsEmbeddedObject());
    DCHECK(!IsCanvas());
    return;
  }

  StyleDifference diff;
  if (style_)
    diff = style_->VisualInvalidationDiff(*style);

  diff = AdjustStyleDifference(diff);

  StyleWillChange(diff, *style);

  RefPtr<ComputedStyle> old_style = std::move(style_);
  SetStyleInternal(std::move(style));

  UpdateFillImages(old_style ? &old_style->BackgroundLayers() : 0,
                   style_->BackgroundLayers());
  UpdateFillImages(old_style ? &old_style->MaskLayers() : 0,
                   style_->MaskLayers());

  UpdateImage(old_style ? old_style->BorderImage().GetImage() : 0,
              style_->BorderImage().GetImage());
  UpdateImage(old_style ? old_style->MaskBoxImage().GetImage() : 0,
              style_->MaskBoxImage().GetImage());

  StyleImage* new_content_image =
      style_->GetContentData() && style_->GetContentData()->IsImage()
          ? ToImageContentData(style_->GetContentData())->GetImage()
          : nullptr;
  StyleImage* old_content_image =
      old_style && old_style->GetContentData() &&
              old_style->GetContentData()->IsImage()
          ? ToImageContentData(old_style->GetContentData())->GetImage()
          : nullptr;
  UpdateImage(old_content_image, new_content_image);

  StyleImage* new_box_reflect_mask_image =
      style_->BoxReflect() ? style_->BoxReflect()->Mask().GetImage() : nullptr;
  StyleImage* old_box_reflect_mask_image =
      old_style && old_style->BoxReflect()
          ? old_style->BoxReflect()->Mask().GetImage()
          : nullptr;
  UpdateImage(old_box_reflect_mask_image, new_box_reflect_mask_image);

  UpdateShapeImage(old_style ? old_style->ShapeOutside() : 0,
                   style_->ShapeOutside());
  UpdateCursorImages(old_style ? old_style->Cursors() : nullptr,
                     style_->Cursors());

  bool does_not_need_layout_or_paint_invalidation = !parent_;

  StyleDidChange(diff, old_style.Get());

  // FIXME: |this| might be destroyed here. This can currently happen for a
  // LayoutTextFragment when its first-letter block gets an update in
  // LayoutTextFragment::styleDidChange. For LayoutTextFragment(s),
  // we will safely bail out with the doesNotNeedLayoutOrPaintInvalidation flag.
  // We might want to broaden this condition in the future as we move
  // layoutObject changes out of layout and into style changes.
  if (does_not_need_layout_or_paint_invalidation)
    return;

  // Now that the layer (if any) has been updated, we need to adjust the diff
  // again, check whether we should layout now, and decide if we need to
  // invalidate paints.
  StyleDifference updated_diff = AdjustStyleDifference(diff);

  if (!diff.NeedsFullLayout()) {
    if (updated_diff.NeedsFullLayout())
      SetNeedsLayoutAndPrefWidthsRecalc(LayoutInvalidationReason::kStyleChange);
    else if (updated_diff.NeedsPositionedMovementLayout())
      SetNeedsPositionedMovementLayout();
  }

  if (diff.TransformChanged() && !NeedsLayout()) {
    if (LayoutBlock* container = ContainingBlock())
      container->SetNeedsOverflowRecalcAfterStyleChange();
  }

  if (diff.NeedsRecomputeOverflow() && !NeedsLayout()) {
    // TODO(rhogan): Make inlines capable of recomputing overflow too.
    if (IsLayoutBlock())
      SetNeedsOverflowRecalcAfterStyleChange();
    else
      SetNeedsLayoutAndPrefWidthsRecalc(LayoutInvalidationReason::kStyleChange);
  }

  if (diff.NeedsPaintInvalidationSubtree() ||
      updated_diff.NeedsPaintInvalidationSubtree()) {
    SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();
  } else if (diff.NeedsPaintInvalidationObject() ||
             updated_diff.NeedsPaintInvalidationObject()) {
    // TODO(wangxianzhu): For now LayoutSVGRoot::localVisualRect() depends on
    // several styles. Refactor to avoid this special case.
    if (IsSVGRoot())
      SetShouldDoFullPaintInvalidation();
    else
      SetShouldDoFullPaintInvalidationWithoutGeometryChange();
  }

  if (diff.NeedsVisualRectUpdate())
    SetMayNeedPaintInvalidation();

  // Text nodes share style with their parents but the paint properties don't
  // apply to them, hence the !isText() check.
  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled() && !IsText() &&
      (diff.TransformChanged() || diff.OpacityChanged() ||
       diff.ZIndexChanged() || diff.FilterChanged() ||
       diff.BackdropFilterChanged() || diff.CssClipChanged())) {
    SetNeedsPaintPropertyUpdate();

    // We don't need to invalidate paint of objects on SPv2 when only paint
    // property or paint order change. Mark the painting layer needing repaint
    // for changed paint property or paint order. Raster invalidation will be
    // issued if needed during paint.
    if (RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
        !ShouldDoFullPaintInvalidation())
      ObjectPaintInvalidator(*this).SlowSetPaintingLayerNeedsRepaint();
  }
}

void LayoutObject::StyleWillChange(StyleDifference diff,
                                   const ComputedStyle& new_style) {
  if (style_) {
    bool visibility_changed = style_->Visibility() != new_style.Visibility();
    // If our z-index changes value or our visibility changes,
    // we need to dirty our stacking context's z-order list.
    if (visibility_changed || style_->ZIndex() != new_style.ZIndex() ||
        style_->IsStackingContext() != new_style.IsStackingContext()) {
      GetDocument().SetAnnotatedRegionsDirty(true);
      if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
        cache->ChildrenChanged(Parent());
    }

    // Keep layer hierarchy visibility bits up to date if visibility changes.
    if (visibility_changed) {
      // We might not have an enclosing layer yet because we might not be in the
      // tree.
      if (PaintLayer* layer = EnclosingLayer())
        layer->DirtyVisibleContentStatus();
    }

    if (IsFloating() && (style_->Floating() != new_style.Floating())) {
      // For changes in float styles, we need to conceivably remove ourselves
      // from the floating objects list.
      ToLayoutBox(this)->RemoveFloatingOrPositionedChildFromBlockLists();
    } else if (IsOutOfFlowPositioned() &&
               (style_->GetPosition() != new_style.GetPosition())) {
      // For changes in positioning styles, we need to conceivably remove
      // ourselves from the positioned objects list.
      ToLayoutBox(this)->RemoveFloatingOrPositionedChildFromBlockLists();
    }

    affects_parent_block_ =
        IsFloatingOrOutOfFlowPositioned() &&
        (!new_style.IsFloating() && !new_style.HasOutOfFlowPosition()) &&
        Parent() &&
        (Parent()->IsLayoutBlockFlow() || Parent()->IsLayoutInline());

    // Clearing these bits is required to avoid leaving stale layoutObjects.
    // FIXME: We shouldn't need that hack if our logic was totally correct.
    if (diff.NeedsLayout()) {
      SetFloating(false);
      ClearPositionedState();
    }
  } else {
    affects_parent_block_ = false;
  }

  // Elements with non-auto touch-action will send a SetTouchAction message
  // on touchstart in EventHandler::handleTouchEvent, and so effectively have
  // a touchstart handler that must be reported.
  //
  // Since a CSS property cannot be applied directly to a text node, a
  // handler will have already been added for its parent so ignore it.
  // TODO: Remove this blocking event handler; crbug.com/318381
  TouchAction old_touch_action =
      style_ ? style_->GetTouchAction() : TouchAction::kTouchActionAuto;
  if (GetNode() && !GetNode()->IsTextNode() &&
      (old_touch_action == TouchAction::kTouchActionAuto) !=
          (new_style.GetTouchAction() == TouchAction::kTouchActionAuto)) {
    EventHandlerRegistry& registry =
        GetDocument().GetPage()->GetEventHandlerRegistry();
    if (new_style.GetTouchAction() != TouchAction::kTouchActionAuto)
      registry.DidAddEventHandler(
          *GetNode(), EventHandlerRegistry::kTouchStartOrMoveEventBlocking);
    else
      registry.DidRemoveEventHandler(
          *GetNode(), EventHandlerRegistry::kTouchStartOrMoveEventBlocking);
  }
}

void LayoutObject::ClearBaseComputedStyle() {
  if (!GetNode())
    return;
  if (!GetNode()->IsElementNode())
    return;
  if (ElementAnimations* animations =
          ToElement(GetNode())->GetElementAnimations())
    animations->ClearBaseComputedStyle();
}

static bool AreNonIdenticalCursorListsEqual(const ComputedStyle* a,
                                            const ComputedStyle* b) {
  DCHECK_NE(a->Cursors(), b->Cursors());
  return a->Cursors() && b->Cursors() && *a->Cursors() == *b->Cursors();
}

static inline bool AreCursorsEqual(const ComputedStyle* a,
                                   const ComputedStyle* b) {
  return a->Cursor() == b->Cursor() && (a->Cursors() == b->Cursors() ||
                                        AreNonIdenticalCursorListsEqual(a, b));
}

void LayoutObject::SetScrollAnchorDisablingStyleChangedOnAncestor() {
  // Walk up the parent chain and find the first scrolling block to disable
  // scroll anchoring on.
  LayoutObject* object = Parent();
  Element* viewport_defining_element = GetDocument().ViewportDefiningElement();
  while (object) {
    if (object->IsLayoutBlock()) {
      LayoutBlock* block = ToLayoutBlock(object);
      if (block->HasOverflowClip() ||
          block->GetNode() == viewport_defining_element) {
        block->SetScrollAnchorDisablingStyleChanged(true);
        return;
      }
    }
    object = object->Parent();
  }
}

void LayoutObject::StyleDidChange(StyleDifference diff,
                                  const ComputedStyle* old_style) {
  // First assume the outline will be affected. It may be updated when we know
  // it's not affected.
  bool has_outline = style_->HasOutline();
  SetOutlineMayBeAffectedByDescendants(has_outline);
  if (!has_outline)
    SetPreviousOutlineMayBeAffectedByDescendants(false);

  if (affects_parent_block_)
    HandleDynamicFloatPositionChange(this);

  if (!parent_)
    return;

  if (diff.NeedsFullLayout()) {
    LayoutCounter::LayoutObjectStyleChanged(*this, old_style, *style_);

    // If the in-flow state of an element is changed, disable scroll
    // anchoring on the containing scroller.
    if (old_style->HasOutOfFlowPosition() != style_->HasOutOfFlowPosition())
      SetScrollAnchorDisablingStyleChangedOnAncestor();

    // If the object already needs layout, then setNeedsLayout won't do
    // any work. But if the containing block has changed, then we may need
    // to mark the new containing blocks for layout. The change that can
    // directly affect the containing block of this object is a change to
    // the position style.
    if (NeedsLayout() && old_style->GetPosition() != style_->GetPosition())
      MarkContainerChainForLayout();

    // Ditto.
    if (NeedsOverflowRecalcAfterStyleChange() &&
        old_style->GetPosition() != style_->GetPosition())
      MarkAncestorsForOverflowRecalcIfNeeded();

    SetNeedsLayoutAndPrefWidthsRecalc(LayoutInvalidationReason::kStyleChange);
  } else if (diff.NeedsPositionedMovementLayout()) {
    SetNeedsPositionedMovementLayout();
  }

  if (diff.ScrollAnchorDisablingPropertyChanged())
    SetScrollAnchorDisablingStyleChanged(true);

  // Don't check for paint invalidation here; we need to wait until the layer
  // has been updated by subclasses before we know if we have to invalidate
  // paints (in setStyle()).

  if (old_style && !AreCursorsEqual(old_style, Style())) {
    if (LocalFrame* frame = this->GetFrame()) {
      // Cursor update scheduling is done by the local root, which is the main
      // frame if there are no RemoteFrame ancestors in the frame tree. Use of
      // localFrameRoot() is discouraged but will change when cursor update
      // scheduling is moved from EventHandler to PageEventHandler.
      frame->LocalFrameRoot().GetEventHandler().ScheduleCursorUpdate();
    }
  }

  if (diff.NeedsFullPaintInvalidation() && old_style) {
    if (ResolveColor(*old_style, CSSPropertyBackgroundColor) !=
            ResolveColor(CSSPropertyBackgroundColor) ||
        old_style->BackgroundLayers() != StyleRef().BackgroundLayers())
      SetBackgroundChangedSinceLastPaintInvalidation();
  }

  if (old_style && old_style->StyleType() == kPseudoIdNone)
    ApplyPseudoStyleChanges(*old_style);

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled() && old_style &&
      old_style->UsedTransformStyle3D() != StyleRef().UsedTransformStyle3D()) {
    // Change of transform-style may affect descendant transform property nodes.
    SetSubtreeNeedsPaintPropertyUpdate();
  }
}

void LayoutObject::ApplyPseudoStyleChanges(const ComputedStyle& old_style) {
  if (old_style.HasPseudoStyle(kPseudoIdFirstLine) ||
      StyleRef().HasPseudoStyle(kPseudoIdFirstLine))
    ApplyFirstLineChanges(old_style);

  if (old_style.HasPseudoStyle(kPseudoIdSelection) ||
      StyleRef().HasPseudoStyle(kPseudoIdSelection))
    InvalidatePaintForSelection();
}

void LayoutObject::ApplyFirstLineChanges(const ComputedStyle& old_style) {
  if (old_style.HasPseudoStyle(kPseudoIdFirstLine)) {
    RefPtr<ComputedStyle> old_pseudo_style =
        old_style.GetCachedPseudoStyle(kPseudoIdFirstLine);
    if (StyleRef().HasPseudoStyle(kPseudoIdFirstLine) && old_pseudo_style) {
      RefPtr<ComputedStyle> new_pseudo_style = UncachedFirstLineStyle();
      if (new_pseudo_style) {
        FirstLineStyleDidChange(*old_pseudo_style, *new_pseudo_style);
        return;
      }
    }
  }
  SetNeedsLayoutAndPrefWidthsRecalc(LayoutInvalidationReason::kStyleChange);
}

void LayoutObject::PropagateStyleToAnonymousChildren() {
  // FIXME: We could save this call when the change only affected non-inherited
  // properties.
  for (LayoutObject* child = SlowFirstChild(); child;
       child = child->NextSibling()) {
    if (!child->IsAnonymous() || child->Style()->StyleType() != kPseudoIdNone)
      continue;

    if (child->AnonymousHasStylePropagationOverride())
      continue;

    RefPtr<ComputedStyle> new_style =
        ComputedStyle::CreateAnonymousStyleWithDisplay(
            StyleRef(), child->Style()->Display());

    // Preserve the position style of anonymous block continuations as they can
    // have relative position when they contain block descendants of relative
    // positioned inlines.
    if (child->IsInFlowPositioned() && child->IsLayoutBlockFlow() &&
        ToLayoutBlockFlow(child)->IsAnonymousBlockContinuation())
      new_style->SetPosition(child->Style()->GetPosition());

    UpdateAnonymousChildStyle(*child, *new_style);

    child->SetStyle(std::move(new_style));
  }
}

void LayoutObject::SetStyleWithWritingModeOf(PassRefPtr<ComputedStyle> style,
                                             LayoutObject* parent) {
  if (parent)
    style->SetWritingMode(parent->StyleRef().GetWritingMode());
  SetStyle(std::move(style));
}

void LayoutObject::SetStyleWithWritingModeOfParent(
    PassRefPtr<ComputedStyle> style) {
  SetStyleWithWritingModeOf(std::move(style), Parent());
}

void LayoutObject::AddChildWithWritingModeOfParent(LayoutObject* new_child,
                                                   LayoutObject* before_child) {
  const WritingMode old_writing_mode =
      new_child->MutableStyleRef().GetWritingMode();
  const WritingMode new_writing_mode = StyleRef().GetWritingMode();
  if (old_writing_mode != new_writing_mode && new_child->IsBoxModelObject()) {
    new_child->MutableStyleRef().SetWritingMode(new_writing_mode);
    new_child->SetHorizontalWritingMode(IsHorizontalWritingMode());
  }
  AddChild(new_child, before_child);
}

void LayoutObject::UpdateFillImages(const FillLayer* old_layers,
                                    const FillLayer& new_layers) {
  // Optimize the common case
  if (FillLayer::ImagesIdentical(old_layers, &new_layers))
    return;

  // Go through the new layers and addClients first, to avoid removing all
  // clients of an image.
  for (const FillLayer* curr_new = &new_layers; curr_new;
       curr_new = curr_new->Next()) {
    if (curr_new->GetImage())
      curr_new->GetImage()->AddClient(this);
  }

  for (const FillLayer* curr_old = old_layers; curr_old;
       curr_old = curr_old->Next()) {
    if (curr_old->GetImage())
      curr_old->GetImage()->RemoveClient(this);
  }
}

void LayoutObject::UpdateCursorImages(const CursorList* old_cursors,
                                      const CursorList* new_cursors) {
  if (old_cursors && new_cursors && *old_cursors == *new_cursors)
    return;

  if (new_cursors) {
    for (const CursorData& cursor_new : *new_cursors) {
      if (cursor_new.GetImage())
        cursor_new.GetImage()->AddClient(this);
    }
  }
  RemoveCursorImageClient(old_cursors);
}

void LayoutObject::UpdateImage(StyleImage* old_image, StyleImage* new_image) {
  if (old_image != new_image) {
    if (old_image)
      old_image->RemoveClient(this);
    if (new_image)
      new_image->AddClient(this);
  }
}

void LayoutObject::UpdateShapeImage(const ShapeValue* old_shape_value,
                                    const ShapeValue* new_shape_value) {
  if (old_shape_value || new_shape_value)
    UpdateImage(old_shape_value ? old_shape_value->GetImage() : 0,
                new_shape_value ? new_shape_value->GetImage() : 0);
}

LayoutRect LayoutObject::ViewRect() const {
  return View()->ViewRect();
}

FloatPoint LayoutObject::LocalToAbsolute(const FloatPoint& local_point,
                                         MapCoordinatesFlags mode) const {
  TransformState transform_state(TransformState::kApplyTransformDirection,
                                 local_point);
  MapLocalToAncestor(0, transform_state, mode | kApplyContainerFlip);
  transform_state.Flatten();

  return transform_state.LastPlanarPoint();
}

FloatPoint LayoutObject::AncestorToLocal(LayoutBoxModelObject* ancestor,
                                         const FloatPoint& container_point,
                                         MapCoordinatesFlags mode) const {
  TransformState transform_state(
      TransformState::kUnapplyInverseTransformDirection, container_point);
  MapAncestorToLocal(ancestor, transform_state, mode);
  transform_state.Flatten();

  return transform_state.LastPlanarPoint();
}

FloatQuad LayoutObject::AncestorToLocalQuad(LayoutBoxModelObject* ancestor,
                                            const FloatQuad& quad,
                                            MapCoordinatesFlags mode) const {
  TransformState transform_state(
      TransformState::kUnapplyInverseTransformDirection,
      quad.BoundingBox().Center(), quad);
  MapAncestorToLocal(ancestor, transform_state, mode);
  transform_state.Flatten();
  return transform_state.LastPlanarQuad();
}

void LayoutObject::MapLocalToAncestor(const LayoutBoxModelObject* ancestor,
                                      TransformState& transform_state,
                                      MapCoordinatesFlags mode) const {
  if (ancestor == this)
    return;

  AncestorSkipInfo skip_info(ancestor);
  const LayoutObject* container = this->Container(&skip_info);
  if (!container)
    return;

  if (mode & kApplyContainerFlip) {
    if (IsBox()) {
      mode &= ~kApplyContainerFlip;
    } else if (container->IsBox()) {
      if (container->Style()->IsFlippedBlocksWritingMode()) {
        IntPoint center_point = RoundedIntPoint(transform_state.MappedPoint());
        transform_state.Move(ToLayoutBox(container)->FlipForWritingMode(
                                 LayoutPoint(center_point)) -
                             center_point);
      }
      mode &= ~kApplyContainerFlip;
    }
  }

  LayoutSize container_offset = OffsetFromContainer(container);

  // TODO(smcgruer): This is inefficient. Instead we should avoid including
  // offsetForInFlowPosition in offsetFromContainer when ignoring sticky.
  if (mode & kIgnoreStickyOffset && IsStickyPositioned()) {
    container_offset -= ToLayoutBoxModelObject(this)->OffsetForInFlowPosition();
  }

  if (IsLayoutFlowThread()) {
    // So far the point has been in flow thread coordinates (i.e. as if
    // everything in the fragmentation context lived in one tall single column).
    // Convert it to a visual point now, since we're about to escape the flow
    // thread.
    container_offset +=
        ColumnOffset(LayoutPoint(transform_state.MappedPoint()));
  }

  // Text objects just copy their parent's computed style, so we need to ignore
  // them.
  bool preserve3d =
      mode & kUseTransforms &&
      ((container->Style()->Preserves3D() && !container->IsText()) ||
       (Style()->Preserves3D() && !IsText()));
  if (mode & kUseTransforms && ShouldUseTransformFromContainer(container)) {
    TransformationMatrix t;
    GetTransformFromContainer(container, container_offset, t);
    transform_state.ApplyTransform(t, preserve3d
                                          ? TransformState::kAccumulateTransform
                                          : TransformState::kFlattenTransform);
  } else {
    transform_state.Move(container_offset.Width(), container_offset.Height(),
                         preserve3d ? TransformState::kAccumulateTransform
                                    : TransformState::kFlattenTransform);
  }

  if (skip_info.AncestorSkipped()) {
    // There can't be a transform between |ancestor| and |o|, because transforms
    // create containers, so it should be safe to just subtract the delta
    // between the ancestor and |o|.
    LayoutSize container_offset =
        ancestor->OffsetFromAncestorContainer(container);
    transform_state.Move(-container_offset.Width(), -container_offset.Height(),
                         preserve3d ? TransformState::kAccumulateTransform
                                    : TransformState::kFlattenTransform);
    // If the ancestor is fixed, then the rect is already in its coordinates so
    // doesn't need viewport-adjusting.
    if (ancestor->Style()->GetPosition() != EPosition::kFixed &&
        container->IsLayoutView() &&
        StyleRef().GetPosition() == EPosition::kFixed) {
      LayoutSize adjustment = ToLayoutView(container)->OffsetForFixedPosition();
      transform_state.Move(adjustment.Width(), adjustment.Height());
    }
    return;
  }

  container->MapLocalToAncestor(ancestor, transform_state, mode);
}

const LayoutObject* LayoutObject::PushMappingToContainer(
    const LayoutBoxModelObject* ancestor_to_stop_at,
    LayoutGeometryMap& geometry_map) const {
  NOTREACHED();
  return nullptr;
}

void LayoutObject::MapAncestorToLocal(const LayoutBoxModelObject* ancestor,
                                      TransformState& transform_state,
                                      MapCoordinatesFlags mode) const {
  if (this == ancestor)
    return;

  AncestorSkipInfo skip_info(ancestor);
  LayoutObject* container = this->Container(&skip_info);
  if (!container)
    return;

  bool apply_container_flip = false;
  if (mode & kApplyContainerFlip) {
    if (IsBox()) {
      mode &= ~kApplyContainerFlip;
    } else if (container->IsBox()) {
      apply_container_flip = container->Style()->IsFlippedBlocksWritingMode();
      mode &= ~kApplyContainerFlip;
    }
  }

  if (!skip_info.AncestorSkipped())
    container->MapAncestorToLocal(ancestor, transform_state, mode);

  LayoutSize container_offset = OffsetFromContainer(container);
  bool preserve3d =
      mode & kUseTransforms &&
      (container->Style()->Preserves3D() || Style()->Preserves3D());
  if (mode & kUseTransforms && ShouldUseTransformFromContainer(container)) {
    TransformationMatrix t;
    GetTransformFromContainer(container, container_offset, t);
    transform_state.ApplyTransform(t, preserve3d
                                          ? TransformState::kAccumulateTransform
                                          : TransformState::kFlattenTransform);
  } else {
    transform_state.Move(container_offset.Width(), container_offset.Height(),
                         preserve3d ? TransformState::kAccumulateTransform
                                    : TransformState::kFlattenTransform);
  }

  if (IsLayoutFlowThread()) {
    // Descending into a flow thread. Convert to the local coordinate space,
    // i.e. flow thread coordinates.
    LayoutPoint visual_point = LayoutPoint(transform_state.MappedPoint());
    transform_state.Move(
        visual_point -
        ToLayoutFlowThread(this)->VisualPointToFlowThreadPoint(visual_point));
  }

  if (apply_container_flip) {
    IntPoint center_point = RoundedIntPoint(transform_state.MappedPoint());
    transform_state.Move(
        center_point -
        ToLayoutBox(container)->FlipForWritingMode(LayoutPoint(center_point)));
  }

  if (skip_info.AncestorSkipped()) {
    container_offset = ancestor->OffsetFromAncestorContainer(container);
    transform_state.Move(-container_offset.Width(), -container_offset.Height());
    // If the ancestor is fixed, then the rect is already in its coordinates so
    // doesn't need viewport-adjusting.
    if (ancestor->Style()->GetPosition() != EPosition::kFixed &&
        container->IsLayoutView() &&
        StyleRef().GetPosition() == EPosition::kFixed) {
      LayoutSize adjustment = ToLayoutView(container)->OffsetForFixedPosition();
      transform_state.Move(adjustment.Width(), adjustment.Height());
    }
  }
}

bool LayoutObject::ShouldUseTransformFromContainer(
    const LayoutObject* container_object) const {
  // hasTransform() indicates whether the object has transform, transform-style
  // or perspective. We just care about transform, so check the layer's
  // transform directly.
  return (HasLayer() && ToLayoutBoxModelObject(this)->Layer()->Transform()) ||
         (container_object && container_object->Style()->HasPerspective());
}

void LayoutObject::GetTransformFromContainer(
    const LayoutObject* container_object,
    const LayoutSize& offset_in_container,
    TransformationMatrix& transform) const {
  transform.MakeIdentity();
  PaintLayer* layer = HasLayer() ? ToLayoutBoxModelObject(this)->Layer() : 0;
  if (layer && layer->Transform())
    transform.Multiply(layer->CurrentTransform());

  transform.PostTranslate(offset_in_container.Width().ToFloat(),
                          offset_in_container.Height().ToFloat());

  if (container_object && container_object->HasLayer() &&
      container_object->Style()->HasPerspective()) {
    // Perspective on the container affects us, so we have to factor it in here.
    DCHECK(container_object->HasLayer());
    FloatPoint perspective_origin =
        ToLayoutBoxModelObject(container_object)->Layer()->PerspectiveOrigin();

    TransformationMatrix perspective_matrix;
    perspective_matrix.ApplyPerspective(
        container_object->Style()->Perspective());
    perspective_matrix.ApplyTransformOrigin(perspective_origin.X(),
                                            perspective_origin.Y(), 0);

    transform = perspective_matrix * transform;
  }
}

FloatQuad LayoutObject::LocalToAncestorQuad(
    const FloatQuad& local_quad,
    const LayoutBoxModelObject* ancestor,
    MapCoordinatesFlags mode) const {
  return LocalToAncestorQuadInternal(local_quad, ancestor,
                                     mode | kUseTransforms);
}

FloatQuad LayoutObject::LocalToAncestorQuadWithoutTransforms(
    const FloatQuad& local_quad,
    const LayoutBoxModelObject* ancestor,
    MapCoordinatesFlags mode) const {
  DCHECK(!(mode & kUseTransforms));
  return LocalToAncestorQuadInternal(local_quad, ancestor, mode);
}

FloatQuad LayoutObject::LocalToAncestorQuadInternal(
    const FloatQuad& local_quad,
    const LayoutBoxModelObject* ancestor,
    MapCoordinatesFlags mode) const {
  // Track the point at the center of the quad's bounding box. As
  // mapLocalToAncestor() calls offsetFromContainer(), it will use that point
  // as the reference point to decide which column's transform to apply in
  // multiple-column blocks.
  // TODO(chrishtr): the second argument to this constructor is unnecessary,
  // since we never call lastPlanarPoint().
  TransformState transform_state(TransformState::kApplyTransformDirection,
                                 local_quad.BoundingBox().Center(), local_quad);
  MapLocalToAncestor(ancestor, transform_state, mode | kApplyContainerFlip);
  transform_state.Flatten();

  return transform_state.LastPlanarQuad();
}

FloatPoint LayoutObject::LocalToAncestorPoint(
    const FloatPoint& local_point,
    const LayoutBoxModelObject* ancestor,
    MapCoordinatesFlags mode) const {
  TransformState transform_state(TransformState::kApplyTransformDirection,
                                 local_point);
  MapLocalToAncestor(ancestor, transform_state,
                     mode | kApplyContainerFlip | kUseTransforms);
  transform_state.Flatten();

  return transform_state.LastPlanarPoint();
}

void LayoutObject::LocalToAncestorRects(Vector<LayoutRect>& rects,
                                        const LayoutBoxModelObject* ancestor,
                                        const LayoutPoint& pre_offset,
                                        const LayoutPoint& post_offset) const {
  for (size_t i = 0; i < rects.size(); ++i) {
    LayoutRect& rect = rects[i];
    rect.MoveBy(pre_offset);
    FloatQuad container_quad =
        LocalToAncestorQuad(FloatQuad(FloatRect(rect)), ancestor);
    LayoutRect container_rect = LayoutRect(container_quad.BoundingBox());
    if (container_rect.IsEmpty()) {
      rects.erase(i--);
      continue;
    }
    container_rect.MoveBy(post_offset);
    rects[i] = container_rect;
  }
}

TransformationMatrix LayoutObject::LocalToAncestorTransform(
    const LayoutBoxModelObject* ancestor,
    MapCoordinatesFlags mode) const {
  TransformState transform_state(TransformState::kApplyTransformDirection);
  MapLocalToAncestor(ancestor, transform_state,
                     mode | kApplyContainerFlip | kUseTransforms);
  return transform_state.AccumulatedTransform();
}

LayoutSize LayoutObject::OffsetFromContainer(const LayoutObject* o) const {
  DCHECK_EQ(o, Container());
  return o->HasOverflowClip()
             ? LayoutSize(-ToLayoutBox(o)->ScrolledContentOffset())
             : LayoutSize();
}

LayoutSize LayoutObject::OffsetFromAncestorContainer(
    const LayoutObject* ancestor_container) const {
  if (ancestor_container == this)
    return LayoutSize();

  LayoutSize offset;
  LayoutPoint reference_point;
  const LayoutObject* curr_container = this;
  do {
    const LayoutObject* next_container = curr_container->Container();
    // This means we reached the top without finding container.
    DCHECK(next_container);
    if (!next_container)
      break;
    DCHECK(!curr_container->HasTransformRelatedProperty());
    LayoutSize current_offset =
        curr_container->OffsetFromContainer(next_container);
    offset += current_offset;
    reference_point.Move(current_offset);
    curr_container = next_container;
  } while (curr_container != ancestor_container);

  return offset;
}

LayoutRect LayoutObject::LocalCaretRect(
    InlineBox*,
    int,
    LayoutUnit* extra_width_to_end_of_line) {
  if (extra_width_to_end_of_line)
    *extra_width_to_end_of_line = LayoutUnit();

  return LayoutRect();
}

void LayoutObject::ComputeLayerHitTestRects(
    LayerHitTestRects& layer_rects) const {
  // Figure out what layer our container is in. Any offset (or new layer) for
  // this layoutObject within it's container will be applied in
  // addLayerHitTestRects.
  LayoutPoint layer_offset;
  const PaintLayer* current_layer = nullptr;

  if (!HasLayer()) {
    LayoutObject* container = this->Container();
    current_layer = container->EnclosingLayer();
    if (container && current_layer->GetLayoutObject() != container) {
      layer_offset.Move(container->OffsetFromAncestorContainer(
          &current_layer->GetLayoutObject()));
      // If the layer itself is scrolled, we have to undo the subtraction of its
      // scroll offset since we want the offset relative to the scrolling
      // content, not the element itself.
      if (current_layer->GetLayoutObject().HasOverflowClip())
        layer_offset.Move(
            current_layer->GetLayoutBox()->ScrolledContentOffset());
    }
  }

  this->AddLayerHitTestRects(layer_rects, current_layer, layer_offset,
                             LayoutRect());
}

void LayoutObject::AddLayerHitTestRects(
    LayerHitTestRects& layer_rects,
    const PaintLayer* current_layer,
    const LayoutPoint& layer_offset,
    const LayoutRect& container_rect) const {
  DCHECK(current_layer);
  DCHECK_EQ(current_layer, this->EnclosingLayer());

  // Compute the rects for this layoutObject only and add them to the results.
  // Note that we could avoid passing the offset and instead adjust each result,
  // but this seems slightly simpler.
  Vector<LayoutRect> own_rects;
  LayoutRect new_container_rect;
  ComputeSelfHitTestRects(own_rects, layer_offset);

  // When we get to have a lot of rects on a layer, the performance cost of
  // tracking those rects outweighs the benefit of doing compositor thread hit
  // testing.
  // FIXME: This limit needs to be low due to the O(n^2) algorithm in
  // WebLayer::setTouchEventHandlerRegion - crbug.com/300282.
  const size_t kMaxRectsPerLayer = 100;

  LayerHitTestRects::iterator iter = layer_rects.find(current_layer);
  Vector<LayoutRect>* iter_value;
  if (iter == layer_rects.end())
    iter_value = &layer_rects.insert(current_layer, Vector<LayoutRect>())
                      .stored_value->value;
  else
    iter_value = &iter->value;
  for (size_t i = 0; i < own_rects.size(); i++) {
    if (!container_rect.Contains(own_rects[i])) {
      iter_value->push_back(own_rects[i]);
      if (iter_value->size() > kMaxRectsPerLayer) {
        // Just mark the entire layer instead, and switch to walking the layer
        // tree instead of the layout tree.
        layer_rects.erase(current_layer);
        current_layer->AddLayerHitTestRects(layer_rects);
        return;
      }
      if (new_container_rect.IsEmpty())
        new_container_rect = own_rects[i];
    }
  }
  if (new_container_rect.IsEmpty())
    new_container_rect = container_rect;

  // If it's possible for children to have rects outside our bounds, then we
  // need to descend into the children and compute them.
  // Ideally there would be other cases where we could detect that children
  // couldn't have rects outside our bounds and prune the tree walk.
  // Note that we don't use Region here because Union is O(N) - better to just
  // keep a list of partially redundant rectangles. If we find examples where
  // this is expensive, then we could rewrite Region to be more efficient. See
  // https://bugs.webkit.org/show_bug.cgi?id=100814.
  if (!IsLayoutView()) {
    for (LayoutObject* curr = SlowFirstChild(); curr;
         curr = curr->NextSibling()) {
      curr->AddLayerHitTestRects(layer_rects, current_layer, layer_offset,
                                 new_container_rect);
    }
  }
}

bool LayoutObject::IsRooted() const {
  const LayoutObject* object = this;
  while (object->Parent() && !object->HasLayer())
    object = object->Parent();
  if (object->HasLayer())
    return ToLayoutBoxModelObject(object)->Layer()->Root()->IsRootLayer();
  return false;
}

RespectImageOrientationEnum LayoutObject::ShouldRespectImageOrientation(
    const LayoutObject* layout_object) {
  if (!layout_object)
    return kDoNotRespectImageOrientation;

  // Respect the image's orientation if it's being used as a full-page image or
  // it's an <img> and the setting to respect it everywhere is set or the <img>
  // has image-orientation: from-image style. FIXME: crbug.com/498233
  if (layout_object->GetDocument().IsImageDocument())
    return kRespectImageOrientation;

  if (!isHTMLImageElement(layout_object->GetNode()))
    return kDoNotRespectImageOrientation;

  if (layout_object->GetDocument().GetSettings() &&
      layout_object->GetDocument()
          .GetSettings()
          ->GetShouldRespectImageOrientation())
    return kRespectImageOrientation;

  if (layout_object->Style() &&
      layout_object->Style()->RespectImageOrientation() ==
          kRespectImageOrientation)
    return kRespectImageOrientation;

  return kDoNotRespectImageOrientation;
}

LayoutObject* LayoutObject::Container(AncestorSkipInfo* skip_info) const {
  // TODO(mstensho): Get rid of this. Nobody should call this method with those
  // flags already set.
  if (skip_info)
    skip_info->ResetOutput();

  if (IsTextOrSVGChild())
    return Parent();

  EPosition pos = style_->GetPosition();
  if (pos == EPosition::kFixed)
    return ContainerForFixedPosition(skip_info);

  if (pos == EPosition::kAbsolute) {
    return ContainerForAbsolutePosition(skip_info);
  }

  if (IsColumnSpanAll()) {
    LayoutObject* multicol_container = SpannerPlaceholder()->Container();
    if (skip_info) {
      // We jumped directly from the spanner to the multicol container. Need to
      // check if we skipped |ancestor| or filter/reflection on the way.
      for (LayoutObject* walker = Parent();
           walker && walker != multicol_container; walker = walker->Parent())
        skip_info->Update(*walker);
    }
    return multicol_container;
  }

  if (IsFloating())
    return ContainingBlock(skip_info);

  return Parent();
}

inline LayoutObject* LayoutObject::ParentCrossingFrames() const {
  if (IsLayoutView())
    return GetFrame()->OwnerLayoutObject();
  return Parent();
}

bool LayoutObject::IsSelectionBorder() const {
  SelectionState st = GetSelectionState();
  return st == SelectionStart || st == SelectionEnd || st == SelectionBoth;
}

inline void LayoutObject::ClearLayoutRootIfNeeded() const {
  if (FrameView* view = GetFrameView()) {
    if (!DocumentBeingDestroyed())
      view->ClearLayoutSubtreeRoot(*this);
  }
}

void LayoutObject::WillBeDestroyed() {
  // Destroy any leftover anonymous children.
  LayoutObjectChildList* children = VirtualChildren();
  if (children)
    children->DestroyLeftoverChildren();

  if (LocalFrame* frame = this->GetFrame()) {
    // If this layoutObject is being autoscrolled, stop the autoscrolling.
    if (frame->GetPage())
      frame->GetPage()->GetAutoscrollController().StopAutoscrollIfNeeded(this);
  }

  // For accessibility management, notify the parent of the imminent change to
  // its child set.
  // We do it now, before remove(), while the parent pointer is still available.
  if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
    cache->ChildrenChanged(this->Parent());

  Remove();

  // The remove() call above may invoke axObjectCache()->childrenChanged() on
  // the parent, which may require the AX layout object for this layoutObject.
  // So we remove the AX layout object now, after the layoutObject is removed.
  if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
    cache->Remove(this);

  // If this layoutObject had a parent, remove should have destroyed any
  // counters attached to this layoutObject and marked the affected other
  // counters for reevaluation. This apparently redundant check is here for the
  // case when this layoutObject had no parent at the time remove() was called.

  if (HasCounterNodeMap())
    LayoutCounter::DestroyCounterNodes(*this);

  // Remove the handler if node had touch-action set. Handlers are not added
  // for text nodes so don't try removing for one too. Need to check if
  // m_style is null in cases of partial construction. Any handler we added
  // previously may have already been removed by the Document independently.
  if (GetNode() && !GetNode()->IsTextNode() && style_ &&
      style_->GetTouchAction() != TouchAction::kTouchActionAuto) {
    EventHandlerRegistry& registry =
        GetDocument().GetPage()->GetEventHandlerRegistry();
    if (registry
            .EventHandlerTargets(
                EventHandlerRegistry::kTouchStartOrMoveEventBlocking)
            ->Contains(GetNode()))
      registry.DidRemoveEventHandler(
          *GetNode(), EventHandlerRegistry::kTouchStartOrMoveEventBlocking);
  }

  SetAncestorLineBoxDirty(false);

  ObjectPaintInvalidator::ObjectWillBeDestroyed(*this);

  ClearLayoutRootIfNeeded();

  if (style_) {
    for (const FillLayer* bg_layer = &style_->BackgroundLayers(); bg_layer;
         bg_layer = bg_layer->Next()) {
      if (StyleImage* background_image = bg_layer->GetImage())
        background_image->RemoveClient(this);
    }

    for (const FillLayer* mask_layer = &style_->MaskLayers(); mask_layer;
         mask_layer = mask_layer->Next()) {
      if (StyleImage* mask_image = mask_layer->GetImage())
        mask_image->RemoveClient(this);
    }

    if (StyleImage* border_image = style_->BorderImage().GetImage())
      border_image->RemoveClient(this);

    if (StyleImage* mask_box_image = style_->MaskBoxImage().GetImage())
      mask_box_image->RemoveClient(this);

    if (style_->GetContentData() && style_->GetContentData()->IsImage())
      ToImageContentData(style_->GetContentData())
          ->GetImage()
          ->RemoveClient(this);

    if (style_->BoxReflect() && style_->BoxReflect()->Mask().GetImage())
      style_->BoxReflect()->Mask().GetImage()->RemoveClient(this);

    RemoveShapeImageClient(style_->ShapeOutside());
    RemoveCursorImageClient(style_->Cursors());
  }

  if (GetFrameView())
    SetIsBackgroundAttachmentFixedObject(false);
}

DISABLE_CFI_PERF
void LayoutObject::InsertedIntoTree() {
  // FIXME: We should DCHECK(isRooted()) here but generated content makes some
  // out-of-order insertion.

  // Keep our layer hierarchy updated. Optimize for the common case where we
  // don't have any children and don't have a layer attached to ourselves.
  PaintLayer* layer = nullptr;
  if (SlowFirstChild() || HasLayer()) {
    layer = Parent()->EnclosingLayer();
    AddLayers(layer);
  }

  // If |this| is visible but this object was not, tell the layer it has some
  // visible content that needs to be drawn and layer visibility optimization
  // can't be used
  if (Parent()->Style()->Visibility() != EVisibility::kVisible &&
      Style()->Visibility() == EVisibility::kVisible && !HasLayer()) {
    if (!layer)
      layer = Parent()->EnclosingLayer();
    if (layer)
      layer->DirtyVisibleContentStatus();
  }

  if (Parent()->ChildrenInline())
    Parent()->DirtyLinesFromChangedChild(this);

  if (LayoutFlowThread* flow_thread = FlowThreadContainingBlock())
    flow_thread->FlowThreadDescendantWasInserted(this);
}

enum FindReferencingScrollAnchorsBehavior { kDontClear, kClear };

static bool FindReferencingScrollAnchors(
    LayoutObject* layout_object,
    FindReferencingScrollAnchorsBehavior behavior) {
  PaintLayer* layer = nullptr;
  if (LayoutObject* parent = layout_object->Parent())
    layer = parent->EnclosingLayer();
  bool found = false;

  // Walk up the layer tree to clear any scroll anchors that reference us.
  while (layer) {
    if (PaintLayerScrollableArea* scrollable_area =
            layer->GetScrollableArea()) {
      ScrollAnchor* anchor = scrollable_area->GetScrollAnchor();
      DCHECK(anchor);
      if (anchor->RefersTo(layout_object)) {
        found = true;
        if (behavior == kClear)
          anchor->NotifyRemoved(layout_object);
        else
          return true;
      }
    }
    layer = layer->Parent();
  }
  if (FrameView* view = layout_object->GetFrameView()) {
    ScrollAnchor* anchor = view->GetScrollAnchor();
    DCHECK(anchor);
    if (anchor->RefersTo(layout_object)) {
      found = true;
      if (behavior == kClear)
        anchor->NotifyRemoved(layout_object);
    }
  }
  return found;
}

void LayoutObject::WillBeRemovedFromTree() {
  // FIXME: We should DCHECK(isRooted()) but we have some out-of-order removals
  // which would need to be fixed first.

  // If we remove a visible child from an invisible parent, we don't know the
  // layer visibility any more.
  PaintLayer* layer = nullptr;
  if (Parent()->Style()->Visibility() != EVisibility::kVisible &&
      Style()->Visibility() == EVisibility::kVisible && !HasLayer()) {
    layer = Parent()->EnclosingLayer();
    if (layer)
      layer->DirtyVisibleContentStatus();
  }

  // Keep our layer hierarchy updated.
  if (SlowFirstChild() || HasLayer()) {
    if (!layer)
      layer = Parent()->EnclosingLayer();
    RemoveLayers(layer);
  }

  if (IsOutOfFlowPositioned() && Parent()->ChildrenInline())
    Parent()->DirtyLinesFromChangedChild(this);

  RemoveFromLayoutFlowThread();

  // Update cached boundaries in SVG layoutObjects if a child is removed.
  if (Parent()->IsSVG())
    Parent()->SetNeedsBoundariesUpdate();

  if (RuntimeEnabledFeatures::scrollAnchoringEnabled() &&
      bitfields_.IsScrollAnchorObject()) {
    // Clear the bit first so that anchor.clear() doesn't recurse into
    // findReferencingScrollAnchors.
    bitfields_.SetIsScrollAnchorObject(false);
    FindReferencingScrollAnchors(this, kClear);
  }
}

void LayoutObject::SetNeedsPaintPropertyUpdate() {
  bitfields_.SetNeedsPaintPropertyUpdate(true);

  LayoutObject* ancestor = ParentCrossingFrames();
  while (ancestor && !ancestor->DescendantNeedsPaintPropertyUpdate()) {
    ancestor->bitfields_.SetDescendantNeedsPaintPropertyUpdate(true);
    ancestor = ancestor->ParentCrossingFrames();
  }
}

void LayoutObject::SetAncestorsNeedPaintPropertyUpdateForMainThreadScrolling() {
  LayoutObject* ancestor = ParentCrossingFrames();
  while (ancestor) {
    ancestor->SetNeedsPaintPropertyUpdate();
    ancestor = ancestor->ParentCrossingFrames();
  }
}

void LayoutObject::MaybeClearIsScrollAnchorObject() {
  if (!bitfields_.IsScrollAnchorObject())
    return;
  bitfields_.SetIsScrollAnchorObject(
      FindReferencingScrollAnchors(this, kDontClear));
}

void LayoutObject::RemoveFromLayoutFlowThread() {
  if (!IsInsideFlowThread())
    return;

  // Sometimes we remove the element from the flow, but it's not destroyed at
  // that time.
  // It's only until later when we actually destroy it and remove all the
  // children from it.
  // Currently, that happens for firstLetter elements and list markers.
  // Pass in the flow thread so that we don't have to look it up for all the
  // children.
  // If we're a column spanner, we need to use our parent to find the flow
  // thread, since a spanner doesn't have the flow thread in its containing
  // block chain. We still need to notify the flow thread when the layoutObject
  // removed happens to be a spanner, so that we get rid of the spanner
  // placeholder, and column sets around the placeholder get merged.
  LayoutFlowThread* flow_thread = IsColumnSpanAll()
                                      ? Parent()->FlowThreadContainingBlock()
                                      : FlowThreadContainingBlock();
  RemoveFromLayoutFlowThreadRecursive(flow_thread);
}

void LayoutObject::RemoveFromLayoutFlowThreadRecursive(
    LayoutFlowThread* layout_flow_thread) {
  if (const LayoutObjectChildList* children = VirtualChildren()) {
    for (LayoutObject* child = children->FirstChild(); child;
         child = child->NextSibling()) {
      if (child->IsLayoutFlowThread())
        continue;  // Don't descend into inner fragmentation contexts.
      child->RemoveFromLayoutFlowThreadRecursive(child->IsLayoutFlowThread()
                                                     ? ToLayoutFlowThread(child)
                                                     : layout_flow_thread);
    }
  }

  if (layout_flow_thread && layout_flow_thread != this)
    layout_flow_thread->FlowThreadDescendantWillBeRemoved(this);
  SetIsInsideFlowThread(false);
  CHECK(!SpannerPlaceholder());
}

void LayoutObject::DestroyAndCleanupAnonymousWrappers() {
  // If the tree is destroyed, there is no need for a clean-up phase.
  if (DocumentBeingDestroyed()) {
    Destroy();
    return;
  }

  LayoutObject* destroy_root = this;
  for (LayoutObject *destroy_root_parent = destroy_root->Parent();
       destroy_root_parent && destroy_root_parent->IsAnonymous();
       destroy_root = destroy_root_parent,
                    destroy_root_parent = destroy_root_parent->Parent()) {
    // Anonymous block continuations are tracked and destroyed elsewhere (see
    // the bottom of LayoutBlock::removeChild)
    if (destroy_root_parent->IsLayoutBlockFlow() &&
        ToLayoutBlockFlow(destroy_root_parent)->IsAnonymousBlockContinuation())
      break;
    // A flow thread is tracked by its containing block. Whether its children
    // are removed or not is irrelevant.
    if (destroy_root_parent->IsLayoutFlowThread())
      break;

    if (destroy_root_parent->SlowFirstChild() != destroy_root ||
        destroy_root_parent->SlowLastChild() != destroy_root)
      break;  // Need to keep the anonymous parent, since it won't become empty
              // by the removal of this layoutObject.
  }

  destroy_root->Destroy();

  // WARNING: |this| is deleted here.
}

void LayoutObject::Destroy() {
  WillBeDestroyed();
  delete this;
}

DISABLE_CFI_PERF
void LayoutObject::RemoveShapeImageClient(ShapeValue* shape_value) {
  if (!shape_value)
    return;
  if (StyleImage* shape_image = shape_value->GetImage())
    shape_image->RemoveClient(this);
}

void LayoutObject::RemoveCursorImageClient(const CursorList* cursor_list) {
  if (!cursor_list)
    return;
  for (const CursorData& cursor : *cursor_list) {
    if (cursor.GetImage())
      cursor.GetImage()->RemoveClient(this);
  }
}

PositionWithAffinity LayoutObject::PositionForPoint(const LayoutPoint&) {
  return CreatePositionWithAffinity(CaretMinOffset());
}

CompositingState LayoutObject::GetCompositingState() const {
  return HasLayer()
             ? ToLayoutBoxModelObject(this)->Layer()->GetCompositingState()
             : kNotComposited;
}

CompositingReasons LayoutObject::AdditionalCompositingReasons() const {
  return kCompositingReasonNone;
}

bool LayoutObject::HitTest(HitTestResult& result,
                           const HitTestLocation& location_in_container,
                           const LayoutPoint& accumulated_offset,
                           HitTestFilter hit_test_filter) {
  bool inside = false;
  if (hit_test_filter != kHitTestSelf) {
    // First test the foreground layer (lines and inlines).
    inside = NodeAtPoint(result, location_in_container, accumulated_offset,
                         kHitTestForeground);

    // Test floats next.
    if (!inside)
      inside = NodeAtPoint(result, location_in_container, accumulated_offset,
                           kHitTestFloat);

    // Finally test to see if the mouse is in the background (within a child
    // block's background).
    if (!inside)
      inside = NodeAtPoint(result, location_in_container, accumulated_offset,
                           kHitTestChildBlockBackgrounds);
  }

  // See if the mouse is inside us but not any of our descendants
  if (hit_test_filter != kHitTestDescendants && !inside)
    inside = NodeAtPoint(result, location_in_container, accumulated_offset,
                         kHitTestBlockBackground);

  return inside;
}

void LayoutObject::UpdateHitTestResult(HitTestResult& result,
                                       const LayoutPoint& point) {
  if (result.InnerNode())
    return;

  Node* node = this->GetNode();

  // If we hit the anonymous layoutObjects inside generated content we should
  // actually hit the generated content so walk up to the PseudoElement.
  if (!node && Parent() && Parent()->IsBeforeOrAfterContent()) {
    for (LayoutObject* layout_object = Parent(); layout_object && !node;
         layout_object = layout_object->Parent())
      node = layout_object->GetNode();
  }

  if (node)
    result.SetNodeAndPosition(node, point);
}

bool LayoutObject::NodeAtPoint(HitTestResult&,
                               const HitTestLocation& /*locationInContainer*/,
                               const LayoutPoint& /*accumulatedOffset*/,
                               HitTestAction) {
  return false;
}

void LayoutObject::ScheduleRelayout() {
  if (IsLayoutView()) {
    FrameView* view = ToLayoutView(this)->GetFrameView();
    if (view)
      view->ScheduleRelayout();
  } else {
    if (IsRooted()) {
      if (LayoutView* layout_view = View()) {
        if (FrameView* frame_view = layout_view->GetFrameView())
          frame_view->ScheduleRelayoutOfSubtree(this);
      }
    }
  }
}

void LayoutObject::ForceLayout() {
  SetSelfNeedsLayout(true);
  SetShouldDoFullPaintInvalidation();
  UpdateLayout();
}

// FIXME: Does this do anything different than forceLayout given that we don't
// walk the containing block chain. If not, we should change all callers to use
// forceLayout.
void LayoutObject::ForceChildLayout() {
  SetNormalChildNeedsLayout(true);
  UpdateLayout();
}

enum StyleCacheState { kCached, kUncached };

static PassRefPtr<ComputedStyle> FirstLineStyleForCachedUncachedType(
    StyleCacheState type,
    const LayoutObject* layout_object,
    ComputedStyle* style) {
  const LayoutObject* layout_object_for_first_line_style = layout_object;
  if (layout_object->IsBeforeOrAfterContent())
    layout_object_for_first_line_style = layout_object->Parent();

  if (layout_object_for_first_line_style->BehavesLikeBlockContainer()) {
    if (const LayoutBlock* first_line_block =
            ToLayoutBlock(layout_object_for_first_line_style)
                ->EnclosingFirstLineStyleBlock()) {
      if (type == kCached)
        return first_line_block->GetCachedPseudoStyle(kPseudoIdFirstLine,
                                                      style);
      return first_line_block->GetUncachedPseudoStyle(
          PseudoStyleRequest(kPseudoIdFirstLine), style);
    }
  } else if (!layout_object_for_first_line_style->IsAnonymous() &&
             layout_object_for_first_line_style->IsLayoutInline() &&
             !layout_object_for_first_line_style->GetNode()
                  ->IsFirstLetterPseudoElement()) {
    const ComputedStyle* parent_style =
        layout_object_for_first_line_style->Parent()->FirstLineStyle();
    if (parent_style != layout_object_for_first_line_style->Parent()->Style()) {
      if (type == kCached) {
        // A first-line style is in effect. Cache a first-line style for
        // ourselves.
        layout_object_for_first_line_style->MutableStyleRef().SetHasPseudoStyle(
            kPseudoIdFirstLineInherited);
        return layout_object_for_first_line_style->GetCachedPseudoStyle(
            kPseudoIdFirstLineInherited, parent_style);
      }
      return layout_object_for_first_line_style->GetUncachedPseudoStyle(
          PseudoStyleRequest(kPseudoIdFirstLineInherited), parent_style);
    }
  }
  return nullptr;
}

PassRefPtr<ComputedStyle> LayoutObject::UncachedFirstLineStyle() const {
  if (!GetDocument().GetStyleEngine().UsesFirstLineRules())
    return nullptr;

  DCHECK(!IsText());

  return FirstLineStyleForCachedUncachedType(kUncached, this, style_.Get());
}

ComputedStyle* LayoutObject::CachedFirstLineStyle() const {
  DCHECK(GetDocument().GetStyleEngine().UsesFirstLineRules());

  if (RefPtr<ComputedStyle> style = FirstLineStyleForCachedUncachedType(
          kCached, IsText() ? Parent() : this, style_.Get()))
    return style.Get();

  return style_.Get();
}

ComputedStyle* LayoutObject::GetCachedPseudoStyle(
    PseudoId pseudo,
    const ComputedStyle* parent_style) const {
  DCHECK_NE(pseudo, kPseudoIdBefore);
  DCHECK_NE(pseudo, kPseudoIdAfter);
  if (!GetNode())
    return nullptr;

  Element* element = Traversal<Element>::FirstAncestorOrSelf(*GetNode());
  if (!element)
    return nullptr;

  return element->PseudoStyle(PseudoStyleRequest(pseudo), parent_style);
}

PassRefPtr<ComputedStyle> LayoutObject::GetUncachedPseudoStyle(
    const PseudoStyleRequest& request,
    const ComputedStyle* parent_style) const {
  DCHECK_NE(request.pseudo_id, kPseudoIdBefore);
  DCHECK_NE(request.pseudo_id, kPseudoIdAfter);
  if (!GetNode())
    return nullptr;

  Element* element = Traversal<Element>::FirstAncestorOrSelf(*GetNode());
  if (!element)
    return nullptr;

  return element->GetUncachedPseudoStyle(request, parent_style);
}

PassRefPtr<ComputedStyle> LayoutObject::GetUncachedSelectionStyle() const {
  if (!GetNode())
    return nullptr;

  // In Blink, ::selection only applies to direct children of the element on
  // which ::selection is matched. In order to be able to style ::selection
  // inside elements implemented with a UA shadow tree, like input::selection,
  // we calculate ::selection style on the shadow host for elements inside the
  // UA shadow.
  if (ShadowRoot* root = GetNode()->ContainingShadowRoot()) {
    if (root->GetType() == ShadowRootType::kUserAgent) {
      if (Element* shadow_host = GetNode()->OwnerShadowHost()) {
        return shadow_host->GetLayoutObject()->GetUncachedPseudoStyle(
            PseudoStyleRequest(kPseudoIdSelection));
      }
    }
  }

  // If we request ::selection style for LayoutText, query ::selection style on
  // the parent element instead, as that is the node for which ::selection
  // matches.
  const LayoutObject* selection_layout_object = this;
  Element* element = Traversal<Element>::FirstAncestorOrSelf(*GetNode());
  if (!element)
    return nullptr;
  if (element != GetNode()) {
    selection_layout_object = element->GetLayoutObject();
    if (!selection_layout_object)
      return nullptr;
  }
  return selection_layout_object->GetUncachedPseudoStyle(
      PseudoStyleRequest(kPseudoIdSelection));
}

void LayoutObject::AddAnnotatedRegions(Vector<AnnotatedRegionValue>& regions) {
  // Convert the style regions to absolute coordinates.
  if (Style()->Visibility() != EVisibility::kVisible || !IsBox())
    return;

  if (Style()->GetDraggableRegionMode() == kDraggableRegionNone)
    return;

  LayoutBox* box = ToLayoutBox(this);
  FloatRect local_bounds(FloatPoint(), FloatSize(box->Size()));
  FloatRect abs_bounds = LocalToAbsoluteQuad(local_bounds).BoundingBox();

  AnnotatedRegionValue region;
  region.draggable = Style()->GetDraggableRegionMode() == kDraggableRegionDrag;
  region.bounds = LayoutRect(abs_bounds);
  regions.push_back(region);
}

bool LayoutObject::WillRenderImage() {
  // Without visibility we won't render (and therefore don't care about
  // animation).
  if (Style()->Visibility() != EVisibility::kVisible)
    return false;

  // We will not render a new image when SuspendableObjects is suspended
  if (GetDocument().IsContextSuspended())
    return false;

  // If we're not in a window (i.e., we're dormant from being in a background
  // tab) then we don't want to render either.
  return GetDocument().View()->IsVisible();
}

bool LayoutObject::GetImageAnimationPolicy(ImageAnimationPolicy& policy) {
  if (!GetDocument().GetSettings())
    return false;
  policy = GetDocument().GetSettings()->GetImageAnimationPolicy();
  return true;
}

int LayoutObject::CaretMinOffset() const {
  return 0;
}

int LayoutObject::CaretMaxOffset() const {
  if (IsAtomicInlineLevel())
    return GetNode() ? std::max(1U, GetNode()->CountChildren()) : 1;
  if (IsHR())
    return 1;
  return 0;
}

bool LayoutObject::IsInert() const {
  const LayoutObject* layout_object = this;
  while (!layout_object->GetNode())
    layout_object = layout_object->Parent();
  return layout_object->GetNode()->IsInert();
}

void LayoutObject::ImageChanged(ImageResourceContent* image,
                                const IntRect* rect) {
  DCHECK(node_);

  // Image change notifications should not be received during paint because
  // the resulting invalidations will be cleared following paint. This can also
  // lead to modifying the tree out from under paint(), see: crbug.com/616700.
  DCHECK_NE(GetDocument().Lifecycle().GetState(),
            DocumentLifecycle::LifecycleState::kInPaint);

  ImageChanged(static_cast<WrappedImagePtr>(image), rect);
}

Element* LayoutObject::OffsetParent(const Element* base) const {
  if (IsDocumentElement() || IsBody())
    return nullptr;

  if (IsFixedPositioned())
    return nullptr;

  float effective_zoom = Style()->EffectiveZoom();
  Node* node = nullptr;
  for (LayoutObject* ancestor = Parent(); ancestor;
       ancestor = ancestor->Parent()) {
    // Spec: http://www.w3.org/TR/cssom-view/#offset-attributes

    node = ancestor->GetNode();

    if (!node)
      continue;

    // TODO(kochi): If |base| or |node| is nested deep in shadow roots, this
    // loop may get expensive, as isUnclosedNodeOf() can take up to O(N+M) time
    // (N and M are depths).
    if (base &&
        (node->IsClosedShadowHiddenFrom(*base) ||
         (node->IsInShadowTree() && node->ContainingShadowRoot()->GetType() ==
                                        ShadowRootType::kUserAgent))) {
      // If 'position: fixed' node is found while traversing up, terminate the
      // loop and return null.
      if (ancestor->IsFixedPositioned())
        return nullptr;
      continue;
    }

    if (ancestor->CanContainAbsolutePositionObjects())
      break;

    if (isHTMLBodyElement(*node))
      break;

    if (!IsPositioned() &&
        (isHTMLTableElement(*node) || IsHTMLTableCellElement(*node)))
      break;

    // Webkit specific extension where offsetParent stops at zoom level changes.
    if (effective_zoom != ancestor->Style()->EffectiveZoom())
      break;
  }

  return node && node->IsElementNode() ? ToElement(node) : nullptr;
}

PositionWithAffinity LayoutObject::CreatePositionWithAffinity(
    int offset,
    TextAffinity affinity) {
  // If this is a non-anonymous layoutObject in an editable area, then it's
  // simple.
  if (Node* node = NonPseudoNode()) {
    if (!HasEditableStyle(*node)) {
      // If it can be found, we prefer a visually equivalent position that is
      // editable.
      const Position position = Position(node, offset);
      Position candidate =
          MostForwardCaretPosition(position, kCanCrossEditingBoundary);
      if (HasEditableStyle(*candidate.AnchorNode()))
        return PositionWithAffinity(candidate, affinity);
      candidate = MostBackwardCaretPosition(position, kCanCrossEditingBoundary);
      if (HasEditableStyle(*candidate.AnchorNode()))
        return PositionWithAffinity(candidate, affinity);
    }
    // FIXME: Eliminate legacy editing positions
    return PositionWithAffinity(Position::EditingPositionOf(node, offset),
                                affinity);
  }

  // We don't want to cross the boundary between editable and non-editable
  // regions of the document, but that is either impossible or at least
  // extremely unlikely in any normal case because we stop as soon as we
  // find a single non-anonymous layoutObject.

  // Find a nearby non-anonymous layoutObject.
  LayoutObject* child = this;
  while (LayoutObject* parent = child->Parent()) {
    // Find non-anonymous content after.
    for (LayoutObject* layout_object = child->NextInPreOrder(parent);
         layout_object; layout_object = layout_object->NextInPreOrder(parent)) {
      if (Node* node = layout_object->NonPseudoNode())
        return PositionWithAffinity(FirstPositionInOrBeforeNode(node));
    }

    // Find non-anonymous content before.
    for (LayoutObject* layout_object = child->PreviousInPreOrder();
         layout_object; layout_object = layout_object->PreviousInPreOrder()) {
      if (layout_object == parent)
        break;
      if (Node* node = layout_object->NonPseudoNode())
        return PositionWithAffinity(LastPositionInOrAfterNode(node));
    }

    // Use the parent itself unless it too is anonymous.
    if (Node* node = parent->NonPseudoNode())
      return PositionWithAffinity(FirstPositionInOrBeforeNode(node));

    // Repeat at the next level up.
    child = parent;
  }

  // Everything was anonymous. Give up.
  return PositionWithAffinity();
}

PositionWithAffinity LayoutObject::CreatePositionWithAffinity(int offset) {
  return CreatePositionWithAffinity(offset, TextAffinity::kDownstream);
}

PositionWithAffinity LayoutObject::CreatePositionWithAffinity(
    const Position& position) {
  if (position.IsNotNull())
    return PositionWithAffinity(position);

  DCHECK(!GetNode());
  return CreatePositionWithAffinity(0);
}

CursorDirective LayoutObject::GetCursor(const LayoutPoint&, Cursor&) const {
  return kSetCursorBasedOnStyle;
}

bool LayoutObject::CanUpdateSelectionOnRootLineBoxes() const {
  if (NeedsLayout())
    return false;

  const LayoutBlock* containing_block = this->ContainingBlock();
  return containing_block ? !containing_block->NeedsLayout() : false;
}

void LayoutObject::SetNeedsBoundariesUpdate() {
  if (LayoutObject* layout_object = Parent())
    layout_object->SetNeedsBoundariesUpdate();
}

FloatRect LayoutObject::ObjectBoundingBox() const {
  NOTREACHED();
  return FloatRect();
}

FloatRect LayoutObject::StrokeBoundingBox() const {
  NOTREACHED();
  return FloatRect();
}

FloatRect LayoutObject::VisualRectInLocalSVGCoordinates() const {
  NOTREACHED();
  return FloatRect();
}

AffineTransform LayoutObject::LocalSVGTransform() const {
  return AffineTransform();
}

bool LayoutObject::NodeAtFloatPoint(HitTestResult&,
                                    const FloatPoint&,
                                    HitTestAction) {
  NOTREACHED();
  return false;
}

bool LayoutObject::IsRelayoutBoundaryForInspector() const {
  return ObjectIsRelayoutBoundary(this);
}

static PaintInvalidationReason DocumentLifecycleBasedPaintInvalidationReason(
    const DocumentLifecycle& document_lifecycle) {
  switch (document_lifecycle.GetState()) {
    case DocumentLifecycle::kInStyleRecalc:
      return PaintInvalidationReason::kStyle;
    case DocumentLifecycle::kInPreLayout:
    case DocumentLifecycle::kInPerformLayout:
    case DocumentLifecycle::kAfterPerformLayout:
      return PaintInvalidationReason::kGeometry;
    case DocumentLifecycle::kInCompositingUpdate:
      return PaintInvalidationReason::kCompositing;
    default:
      return PaintInvalidationReason::kFull;
  }
}

inline void LayoutObject::MarkAncestorsForPaintInvalidation() {
  for (LayoutObject* parent = this->ParentCrossingFrames();
       parent && !parent->ShouldCheckForPaintInvalidation();
       parent = parent->ParentCrossingFrames())
    parent->bitfields_.SetMayNeedPaintInvalidation(true);
}

inline void LayoutObject::SetNeedsPaintOffsetAndVisualRectUpdate() {
  if (NeedsPaintOffsetAndVisualRectUpdate())
    return;
  for (auto* object = this;
       object && !object->NeedsPaintOffsetAndVisualRectUpdate();
       object = object->ParentCrossingFrames()) {
    object->bitfields_.SetNeedsPaintOffsetAndVisualRectUpdate(true);

    // Focus ring is special because continuations affect shape of focus ring.
    // Mark the start object for paint invalidation if it has focus ring.
    if (!object->IsAnonymous() || !object->IsLayoutBlockFlow())
      continue;
    auto* block_flow = ToLayoutBlockFlow(object);
    if (!block_flow->IsAnonymousBlockContinuation())
      continue;
    if (auto* inline_element_continuation =
            block_flow->InlineElementContinuation()) {
      auto* start_of_continuations =
          inline_element_continuation->GetNode()->GetLayoutObject();
      if (start_of_continuations &&
          start_of_continuations->StyleRef().OutlineStyleIsAuto())
        start_of_continuations->SetMayNeedPaintInvalidation();
    }
  }
}

void LayoutObject::SetShouldInvalidateSelection() {
  if (!CanUpdateSelectionOnRootLineBoxes())
    return;
  bitfields_.SetShouldInvalidateSelection(true);
  SetMayNeedPaintInvalidation();
  GetFrameView()->ScheduleVisualUpdateForPaintInvalidationIfNeeded();
}

bool LayoutObject::ShouldCheckForPaintInvalidationWithPaintInvalidationState(
    const PaintInvalidationState& paint_invalidation_state) const {
  return paint_invalidation_state.HasForcedSubtreeInvalidationFlags() ||
         ShouldCheckForPaintInvalidation();
}

void LayoutObject::SetShouldDoFullPaintInvalidation(
    PaintInvalidationReason reason) {
  SetNeedsPaintOffsetAndVisualRectUpdate();
  SetShouldDoFullPaintInvalidationWithoutGeometryChange(reason);
}

void LayoutObject::SetShouldDoFullPaintInvalidationWithoutGeometryChange(
    PaintInvalidationReason reason) {
  // Only full invalidation reasons are allowed.
  DCHECK(IsFullPaintInvalidationReason(reason));

  bool is_upgrading_delayed_full_to_full =
      bitfields_.FullPaintInvalidationReason() ==
          PaintInvalidationReason::kDelayedFull &&
      reason != PaintInvalidationReason::kDelayedFull;

  if (bitfields_.FullPaintInvalidationReason() ==
          PaintInvalidationReason::kNone ||
      is_upgrading_delayed_full_to_full) {
    if (reason == PaintInvalidationReason::kFull) {
      reason = DocumentLifecycleBasedPaintInvalidationReason(
          GetDocument().Lifecycle());
    }
    bitfields_.SetFullPaintInvalidationReason(reason);
    if (!is_upgrading_delayed_full_to_full)
      MarkAncestorsForPaintInvalidation();
  }

  GetFrameView()->ScheduleVisualUpdateForPaintInvalidationIfNeeded();
}

void LayoutObject::SetMayNeedPaintInvalidation() {
  SetNeedsPaintOffsetAndVisualRectUpdate();
  SetMayNeedPaintInvalidationWithoutGeometryChange();
}

void LayoutObject::SetMayNeedPaintInvalidationWithoutGeometryChange() {
  if (MayNeedPaintInvalidation())
    return;
  bitfields_.SetMayNeedPaintInvalidation(true);
  MarkAncestorsForPaintInvalidation();
  GetFrameView()->ScheduleVisualUpdateForPaintInvalidationIfNeeded();
}

void LayoutObject::SetMayNeedPaintInvalidationSubtree() {
  if (MayNeedPaintInvalidationSubtree())
    return;
  bitfields_.SetMayNeedPaintInvalidationSubtree(true);
  SetMayNeedPaintInvalidation();
}

void LayoutObject::SetMayNeedPaintInvalidationAnimatedBackgroundImage() {
  if (MayNeedPaintInvalidationAnimatedBackgroundImage())
    return;
  bitfields_.SetMayNeedPaintInvalidationAnimatedBackgroundImage(true);
  SetMayNeedPaintInvalidationWithoutGeometryChange();
}

void LayoutObject::ClearPaintInvalidationFlags() {
// paintInvalidationStateIsDirty should be kept in sync with the
// booleans that are cleared below.
#if DCHECK_IS_ON()
  DCHECK(!ShouldCheckForPaintInvalidation() || PaintInvalidationStateIsDirty());
#endif
  ClearShouldDoFullPaintInvalidation();
  bitfields_.SetMayNeedPaintInvalidation(false);
  bitfields_.SetMayNeedPaintInvalidationSubtree(false);
  bitfields_.SetMayNeedPaintInvalidationAnimatedBackgroundImage(false);
  bitfields_.SetNeedsPaintOffsetAndVisualRectUpdate(false);
  bitfields_.SetShouldInvalidateSelection(false);
  bitfields_.SetBackgroundChangedSinceLastPaintInvalidation(false);
}

bool LayoutObject::IsAllowedToModifyLayoutTreeStructure(Document& document) {
  return DeprecatedDisableModifyLayoutTreeStructureAsserts::
             CanModifyLayoutTreeStateInAnyState() ||
         document.Lifecycle().StateAllowsLayoutTreeMutations();
}

DeprecatedDisableModifyLayoutTreeStructureAsserts::
    DeprecatedDisableModifyLayoutTreeStructureAsserts()
    : disabler_(&g_modify_layout_tree_structure_any_state, true) {}

bool DeprecatedDisableModifyLayoutTreeStructureAsserts::
    CanModifyLayoutTreeStateInAnyState() {
  return g_modify_layout_tree_structure_any_state;
}

void LayoutObject::
    SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants() {
  // Clear first because PaintInvalidationSubtree overrides other full paint
  // invalidation reasons.
  ClearShouldDoFullPaintInvalidation();
  SetShouldDoFullPaintInvalidation(PaintInvalidationReason::kSubtree);
}

void LayoutObject::SetIsBackgroundAttachmentFixedObject(
    bool is_background_attachment_fixed_object) {
  DCHECK(GetFrameView());
  if (bitfields_.IsBackgroundAttachmentFixedObject() ==
      is_background_attachment_fixed_object)
    return;
  bitfields_.SetIsBackgroundAttachmentFixedObject(
      is_background_attachment_fixed_object);
  if (is_background_attachment_fixed_object)
    GetFrameView()->AddBackgroundAttachmentFixedObject(this);
  else
    GetFrameView()->RemoveBackgroundAttachmentFixedObject(this);
}

PropertyTreeState LayoutObject::ContentsProperties() const {
  DCHECK(RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled());
  return rare_paint_data_->ContentsProperties();
}

RarePaintData& LayoutObject::EnsureRarePaintData() {
  if (!rare_paint_data_)
    rare_paint_data_ = WTF::MakeUnique<RarePaintData>();
  return *rare_paint_data_.get();
}

LayoutRect LayoutObject::DebugRect() const {
  LayoutRect rect;
  LayoutBlock* block = ContainingBlock();
  if (block)
    block->AdjustChildDebugRect(rect);

  return rect;
}

FragmentData* LayoutObject::MutableForPainting::FirstFragment() {
  if (auto* paint_data = layout_object_.GetRarePaintData())
    return paint_data->Fragment();
  return nullptr;
}

FragmentData& LayoutObject::MutableForPainting::EnsureFirstFragment() {
  return layout_object_.EnsureRarePaintData().EnsureFragment();
}

void LayoutObject::InvalidatePaintForSelection() {
  // setSelectionState() propagates the state up the containing block chain to
  // tell if a block contains selected nodes or not. If this layout object is
  // not a block, we need to get the selection state from the containing block
  // to tell if we have any selected node children.
  LayoutBlock* block =
      IsLayoutBlock() ? ToLayoutBlock(this) : ContainingBlock();
  if (!block)
    return;
  if (!block->HasSelectedChildren())
    return;

  // ::selection style only applies to direct selection leaf children of the
  // element on which the ::selection style is set. Thus, we only walk the
  // direct children here.
  for (LayoutObject* child = SlowFirstChild(); child;
       child = child->NextSibling()) {
    if (!child->CanBeSelectionLeaf())
      continue;
    if (child->GetSelectionState() == SelectionNone)
      continue;
    child->SetShouldInvalidateSelection();
  }
}

}  // namespace blink

#ifndef NDEBUG

void showTree(const blink::LayoutObject* object) {
  if (object)
    object->ShowTreeForThis();
  else
    WTFLogAlways("%s", "Cannot showTree. Root is (nil)");
}

void showLineTree(const blink::LayoutObject* object) {
  if (object)
    object->ShowLineTreeForThis();
  else
    WTFLogAlways("%s", "Cannot showLineTree. Root is (nil)");
}

void showLayoutTree(const blink::LayoutObject* object1) {
  showLayoutTree(object1, 0);
}

void showLayoutTree(const blink::LayoutObject* object1,
                    const blink::LayoutObject* object2) {
  if (object1) {
    const blink::LayoutObject* root = object1;
    while (root->Parent())
      root = root->Parent();
    root->ShowLayoutTreeAndMark(object1, "*", object2, "-", 0);
  } else {
    WTFLogAlways("%s", "Cannot showLayoutTree. Root is (nil)");
  }
}

#endif
