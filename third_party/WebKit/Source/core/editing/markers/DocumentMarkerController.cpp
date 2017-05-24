/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
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

#include "core/editing/markers/DocumentMarkerController.h"

#include <algorithm>
#include "core/dom/Node.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/Range.h"
#include "core/dom/Text.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/markers/CompositionMarkerListImpl.h"
#include "core/editing/markers/DocumentMarkerListEditor.h"
#include "core/editing/markers/GrammarMarkerListImpl.h"
#include "core/editing/markers/RenderedDocumentMarker.h"
#include "core/editing/markers/SpellingMarkerListImpl.h"
#include "core/editing/markers/TextMatchMarkerListImpl.h"
#include "core/frame/FrameView.h"
#include "core/layout/LayoutObject.h"

#ifndef NDEBUG
#include <stdio.h>
#endif

namespace blink {

namespace {

DocumentMarker::MarkerTypeIndex MarkerTypeToMarkerIndex(
    DocumentMarker::MarkerType type) {
  switch (type) {
    case DocumentMarker::kSpelling:
      return DocumentMarker::kSpellingMarkerIndex;
    case DocumentMarker::kGrammar:
      return DocumentMarker::kGrammarMarkerIndex;
    case DocumentMarker::kTextMatch:
      return DocumentMarker::kTextMatchMarkerIndex;
    case DocumentMarker::kComposition:
      return DocumentMarker::kCompositionMarkerIndex;
  }

  NOTREACHED();
  return DocumentMarker::kSpellingMarkerIndex;
}

DocumentMarkerList* CreateListForType(DocumentMarker::MarkerType type) {
  switch (type) {
    case DocumentMarker::kComposition:
      return new CompositionMarkerListImpl();
    case DocumentMarker::kSpelling:
      return new SpellingMarkerListImpl();
    case DocumentMarker::kGrammar:
      return new GrammarMarkerListImpl();
    case DocumentMarker::kTextMatch:
      return new TextMatchMarkerListImpl();
  }

  NOTREACHED();
  return nullptr;
}

}  // namespace

Member<DocumentMarkerList>& DocumentMarkerController::ListForType(
    MarkerLists* marker_lists,
    DocumentMarker::MarkerType type) {
  const size_t marker_list_index = MarkerTypeToMarkerIndex(type);
  return (*marker_lists)[marker_list_index];
}

inline bool DocumentMarkerController::PossiblyHasMarkers(
    DocumentMarker::MarkerTypes types) {
  if (markers_.IsEmpty()) {
    // It's possible for markers_ to become empty through garbage collection if
    // all its Nodes are GC'ed since we only hold weak references, in which case
    // possibly_existing_marker_types_ isn't reset to 0 as it is in the other
    // codepaths that remove from markers_. Therefore, we check for this case
    // here.

    // Alternatively, we could handle this case at the time the Node is GC'ed,
    // but that operation is more performance-sensitive than anywhere
    // PossiblyHasMarkers() is used.
    possibly_existing_marker_types_ = 0;
    return false;
  }

  return possibly_existing_marker_types_.Intersects(types);
}

DocumentMarkerController::DocumentMarkerController(Document& document)
    : possibly_existing_marker_types_(0), document_(&document) {
  SetContext(&document);
}

void DocumentMarkerController::Clear() {
  markers_.clear();
  possibly_existing_marker_types_ = 0;
}

void DocumentMarkerController::AddMarker(const Position& start,
                                         const Position& end,
                                         DocumentMarker::MarkerType type,
                                         const String& description) {
  // Use a TextIterator to visit the potentially multiple nodes the range
  // covers.
  for (TextIterator marked_text(start, end); !marked_text.AtEnd();
       marked_text.Advance()) {
    AddMarker(marked_text.CurrentContainer(),
              new DocumentMarker(
                  type, marked_text.StartOffsetInCurrentContainer(),
                  marked_text.EndOffsetInCurrentContainer(), description));
  }
}

void DocumentMarkerController::AddTextMatchMarker(
    const EphemeralRange& range,
    DocumentMarker::MatchStatus match_status) {
  DCHECK(!document_->NeedsLayoutTreeUpdate());

  // Use a TextIterator to visit the potentially multiple nodes the range
  // covers.
  for (TextIterator marked_text(range.StartPosition(), range.EndPosition());
       !marked_text.AtEnd(); marked_text.Advance()) {
    AddMarker(marked_text.CurrentContainer(),
              new DocumentMarker(marked_text.StartOffsetInCurrentContainer(),
                                 marked_text.EndOffsetInCurrentContainer(),
                                 match_status));
  }
  // Don't invalidate tickmarks here. TextFinder invalidates tickmarks using a
  // throttling algorithm. crbug.com/6819.
}

void DocumentMarkerController::AddCompositionMarker(const Position& start,
                                                    const Position& end,
                                                    Color underline_color,
                                                    bool thick,
                                                    Color background_color) {
  DCHECK(!document_->NeedsLayoutTreeUpdate());

  for (TextIterator marked_text(start, end); !marked_text.AtEnd();
       marked_text.Advance()) {
    AddMarker(marked_text.CurrentContainer(),
              new DocumentMarker(marked_text.StartOffsetInCurrentContainer(),
                                 marked_text.EndOffsetInCurrentContainer(),
                                 underline_color, thick, background_color));
  }
}

void DocumentMarkerController::PrepareForDestruction() {
  Clear();
}

void DocumentMarkerController::RemoveMarkers(
    TextIterator& marked_text,
    DocumentMarker::MarkerTypes marker_types) {
  for (; !marked_text.AtEnd(); marked_text.Advance()) {
    if (!PossiblyHasMarkers(marker_types))
      return;
    DCHECK(!markers_.IsEmpty());

    int start_offset = marked_text.StartOffsetInCurrentContainer();
    int end_offset = marked_text.EndOffsetInCurrentContainer();
    RemoveMarkersInternal(marked_text.CurrentContainer(), start_offset,
                          end_offset - start_offset, marker_types);
  }
}

void DocumentMarkerController::RemoveMarkersInRange(
    const EphemeralRange& range,
    DocumentMarker::MarkerTypes marker_types) {
  DCHECK(!document_->NeedsLayoutTreeUpdate());

  TextIterator marked_text(range.StartPosition(), range.EndPosition());
  DocumentMarkerController::RemoveMarkers(marked_text, marker_types);
}

static void UpdateMarkerRenderedRect(const Node& node,
                                     RenderedDocumentMarker& marker) {
  const Position startPosition(&const_cast<Node&>(node), marker.StartOffset());
  const Position endPostion(&const_cast<Node&>(node), marker.EndOffset());
  EphemeralRange range(startPosition, endPostion);
  marker.SetRenderedRect(LayoutRect(ComputeTextRect(range)));
}

// Markers are stored in order sorted by their start offset.
// Markers of the same type do not overlap each other.

void DocumentMarkerController::AddMarker(Node* node,
                                         DocumentMarker* new_marker) {
  DCHECK_GE(new_marker->EndOffset(), new_marker->StartOffset());
  if (new_marker->EndOffset() == new_marker->StartOffset())
    return;

  possibly_existing_marker_types_.Add(new_marker->GetType());

  Member<MarkerLists>& markers =
      markers_.insert(node, nullptr).stored_value->value;
  if (!markers) {
    markers = new MarkerLists;
    markers->Grow(DocumentMarker::kMarkerTypeIndexesCount);
  }

  const DocumentMarker::MarkerType new_marker_type = new_marker->GetType();
  if (!ListForType(markers, new_marker_type))
    ListForType(markers, new_marker_type) = CreateListForType(new_marker_type);

  DocumentMarkerList* const list = ListForType(markers, new_marker_type);
  list->Add(new_marker);

  // repaint the affected node
  if (node->GetLayoutObject()) {
    node->GetLayoutObject()->SetShouldDoFullPaintInvalidation(
        PaintInvalidationReason::kDocumentMarker);
  }
}

// Moves markers from src_node to dst_node. Markers are moved if their start
// offset is less than length. Markers that run past that point are truncated.
void DocumentMarkerController::MoveMarkers(Node* src_node,
                                           int length,
                                           Node* dst_node) {
  if (length <= 0)
    return;

  if (!PossiblyHasMarkers(DocumentMarker::AllMarkers()))
    return;
  DCHECK(!markers_.IsEmpty());

  MarkerLists* src_markers = markers_.at(src_node);
  if (!src_markers)
    return;

  if (!markers_.Contains(dst_node)) {
    markers_.insert(dst_node,
                    new MarkerLists(DocumentMarker::kMarkerTypeIndexesCount));
  }
  MarkerLists* dst_markers = markers_.at(dst_node);

  bool doc_dirty = false;
  for (DocumentMarker::MarkerType type : DocumentMarker::AllMarkers()) {
    DocumentMarkerList* const src_list = ListForType(src_markers, type);
    if (!src_list)
      continue;

    if (!ListForType(dst_markers, type))
      ListForType(dst_markers, type) = CreateListForType(type);

    DocumentMarkerList* const dst_list = ListForType(dst_markers, type);
    if (src_list->MoveMarkers(length, dst_list))
      doc_dirty = true;
  }

  // repaint the affected node
  if (doc_dirty && dst_node->GetLayoutObject()) {
    dst_node->GetLayoutObject()->SetShouldDoFullPaintInvalidation(
        PaintInvalidationReason::kDocumentMarker);
  }
}

void DocumentMarkerController::RemoveMarkersInternal(
    Node* node,
    unsigned start_offset,
    int length,
    DocumentMarker::MarkerTypes marker_types) {
  if (length <= 0)
    return;

  if (!PossiblyHasMarkers(marker_types))
    return;
  DCHECK(!(markers_.IsEmpty()));

  MarkerLists* markers = markers_.at(node);
  if (!markers)
    return;

  bool doc_dirty = false;
  size_t empty_lists_count = 0;
  for (DocumentMarker::MarkerType type : DocumentMarker::AllMarkers()) {
    DocumentMarkerList* const list = ListForType(markers, type);
    if (!list || list->IsEmpty()) {
      if (list && list->IsEmpty())
        ListForType(markers, type) = nullptr;
      ++empty_lists_count;
      continue;
    }
    if (!marker_types.Contains(type))
      continue;

    if (list->RemoveMarkers(start_offset, length))
      doc_dirty = true;

    if (list->IsEmpty()) {
      ListForType(markers, type) = nullptr;
      ++empty_lists_count;
    }
  }

  if (empty_lists_count == DocumentMarker::kMarkerTypeIndexesCount) {
    markers_.erase(node);
    if (markers_.IsEmpty())
      possibly_existing_marker_types_ = 0;
  }

  // repaint the affected node
  if (doc_dirty && node->GetLayoutObject()) {
    node->GetLayoutObject()->SetShouldDoFullPaintInvalidation(
        PaintInvalidationReason::kDocumentMarker);
  }
}

DocumentMarker* DocumentMarkerController::MarkerAtPosition(
    const Position& position,
    DocumentMarker::MarkerTypes marker_types) {
  if (!PossiblyHasMarkers(marker_types))
    return nullptr;
  Node* const node = position.ComputeContainerNode();
  const unsigned offset =
      static_cast<unsigned>(position.ComputeOffsetInContainerNode());

  const auto& markers = MarkersFor(node, marker_types);
  const auto& it =
      std::find_if(markers.begin(), markers.end(), [=](DocumentMarker* marker) {
        return marker->StartOffset() < offset && offset < marker->EndOffset();
      });
  return it == markers.end() ? nullptr : *it;
}

DocumentMarkerVector DocumentMarkerController::MarkersFor(
    Node* node,
    DocumentMarker::MarkerTypes marker_types) {
  DocumentMarkerVector result;

  MarkerLists* markers = markers_.at(node);
  if (!markers)
    return result;

  for (DocumentMarker::MarkerType type : DocumentMarker::AllMarkers()) {
    DocumentMarkerList* const list = ListForType(markers, type);
    if (!list || list->IsEmpty() || !marker_types.Contains(type))
      continue;

    result.AppendVector(list->GetMarkers());
  }

  std::sort(result.begin(), result.end(),
            [](const Member<DocumentMarker>& marker1,
               const Member<DocumentMarker>& marker2) {
              return marker1->StartOffset() < marker2->StartOffset();
            });
  return result;
}

DocumentMarkerVector DocumentMarkerController::Markers() {
  DocumentMarkerVector result;
  for (MarkerMap::iterator i = markers_.begin(); i != markers_.end(); ++i) {
    MarkerLists* markers = i->value.Get();
    for (DocumentMarker::MarkerType type : DocumentMarker::AllMarkers()) {
      DocumentMarkerList* const list = ListForType(markers, type);
      if (!list)
        continue;
      result.AppendVector(list->GetMarkers());
    }
  }
  std::sort(result.begin(), result.end(),
            [](const Member<DocumentMarker>& marker1,
               const Member<DocumentMarker>& marker2) {
              return marker1->StartOffset() < marker2->StartOffset();
            });
  return result;
}

Vector<IntRect> DocumentMarkerController::RenderedRectsForTextMatchMarkers() {
  Vector<IntRect> result;
  if (!PossiblyHasMarkers(DocumentMarker::kTextMatch))
    return result;
  DCHECK(!(markers_.IsEmpty()));

  // outer loop: process each node
  MarkerMap::iterator end = markers_.end();
  for (MarkerMap::iterator node_iterator = markers_.begin();
       node_iterator != end; ++node_iterator) {
    // inner loop; process each marker in this node
    const Node& node = *node_iterator->key;
    if (!node.isConnected())
      continue;
    MarkerLists* markers = node_iterator->value.Get();
    DocumentMarkerList* const list =
        ListForType(markers, DocumentMarker::kTextMatch);
    if (!list || list->IsEmpty())
      continue;

    for (DocumentMarker* marker : list->GetMarkers()) {
      RenderedDocumentMarker* const rendered_marker =
          ToRenderedDocumentMarker(marker);
      UpdateMarkerRenderedRectIfNeeded(node, *rendered_marker);
      if (!rendered_marker->IsRendered())
        continue;
      result.push_back(rendered_marker->RenderedRect());
    }
  }

  return result;
}

static void InvalidatePaintForTickmarks(const Node& node) {
  if (FrameView* frame_view = node.GetDocument().View())
    frame_view->InvalidatePaintForTickmarks();
}

void DocumentMarkerController::UpdateMarkerRenderedRectIfNeeded(
    const Node& node,
    RenderedDocumentMarker& marker) {
  DCHECK(!document_->View() || !document_->View()->NeedsLayout());
  DCHECK(!document_->NeedsLayoutTreeUpdate());
  if (!marker.IsValid())
    UpdateMarkerRenderedRect(node, marker);
}

void DocumentMarkerController::InvalidateRectsForTextMatchMarkersInNode(
    const Node& node) {
  MarkerLists* markers = markers_.at(&node);

  const DocumentMarkerList* const marker_list =
      ListForType(markers, DocumentMarker::kTextMatch);
  if (!marker_list || marker_list->IsEmpty())
    return;

  const HeapVector<Member<DocumentMarker>>& markers_in_list =
      marker_list->GetMarkers();
  for (auto& marker : markers_in_list)
    ToRenderedDocumentMarker(marker)->Invalidate();

  InvalidatePaintForTickmarks(node);
}

void DocumentMarkerController::InvalidateRectsForAllTextMatchMarkers() {
  for (auto& node_markers : markers_) {
    const Node& node = *node_markers.key;
    InvalidateRectsForTextMatchMarkersInNode(node);
  }
}

DEFINE_TRACE(DocumentMarkerController) {
  visitor->Trace(markers_);
  visitor->Trace(document_);
  SynchronousMutationObserver::Trace(visitor);
}

void DocumentMarkerController::RemoveMarkersForNode(
    Node* node,
    DocumentMarker::MarkerTypes marker_types) {
  if (!PossiblyHasMarkers(marker_types))
    return;
  DCHECK(!markers_.IsEmpty());

  MarkerMap::iterator iterator = markers_.find(node);
  if (iterator != markers_.end())
    RemoveMarkersFromList(iterator, marker_types);
}

void DocumentMarkerController::RemoveSpellingMarkersUnderWords(
    const Vector<String>& words) {
  for (auto& node_markers : markers_) {
    const Node& node = *node_markers.key;
    if (!node.IsTextNode())
      continue;
    MarkerLists* markers = node_markers.value;
    for (DocumentMarker::MarkerType type :
         DocumentMarker::MisspellingMarkers()) {
      DocumentMarkerList* const list = ListForType(markers, type);
      if (!list)
        continue;
      ToSpellCheckMarkerListImpl(list)->RemoveMarkersUnderWords(
          ToText(node).data(), words);
    }
  }
}

void DocumentMarkerController::RemoveMarkersOfTypes(
    DocumentMarker::MarkerTypes marker_types) {
  if (!PossiblyHasMarkers(marker_types))
    return;
  DCHECK(!markers_.IsEmpty());

  HeapVector<Member<const Node>> nodes_with_markers;
  CopyKeysToVector(markers_, nodes_with_markers);
  unsigned size = nodes_with_markers.size();
  for (unsigned i = 0; i < size; ++i) {
    MarkerMap::iterator iterator = markers_.find(nodes_with_markers[i]);
    if (iterator != markers_.end())
      RemoveMarkersFromList(iterator, marker_types);
  }

  possibly_existing_marker_types_.Remove(marker_types);
}

void DocumentMarkerController::RemoveMarkersFromList(
    MarkerMap::iterator iterator,
    DocumentMarker::MarkerTypes marker_types) {
  bool needs_repainting = false;
  bool node_can_be_removed;

  size_t empty_lists_count = 0;
  if (marker_types == DocumentMarker::AllMarkers()) {
    needs_repainting = true;
    node_can_be_removed = true;
  } else {
    MarkerLists* markers = iterator->value.Get();

    for (DocumentMarker::MarkerType type : DocumentMarker::AllMarkers()) {
      DocumentMarkerList* const list = ListForType(markers, type);
      if (!list || list->IsEmpty()) {
        if (list && list->IsEmpty())
          ListForType(markers, type) = nullptr;
        ++empty_lists_count;
        continue;
      }
      if (marker_types.Contains(type)) {
        list->Clear();
        ListForType(markers, type) = nullptr;
        ++empty_lists_count;
        needs_repainting = true;
      }
    }

    node_can_be_removed =
        empty_lists_count == DocumentMarker::kMarkerTypeIndexesCount;
  }

  if (needs_repainting) {
    const Node& node = *iterator->key;
    if (LayoutObject* layout_object = node.GetLayoutObject()) {
      layout_object->SetShouldDoFullPaintInvalidation(
          PaintInvalidationReason::kDocumentMarker);
    }
    InvalidatePaintForTickmarks(node);
  }

  if (node_can_be_removed) {
    markers_.erase(iterator);
    if (markers_.IsEmpty())
      possibly_existing_marker_types_ = 0;
  }
}

void DocumentMarkerController::RepaintMarkers(
    DocumentMarker::MarkerTypes marker_types) {
  if (!PossiblyHasMarkers(marker_types))
    return;
  DCHECK(!markers_.IsEmpty());

  // outer loop: process each markered node in the document
  MarkerMap::iterator end = markers_.end();
  for (MarkerMap::iterator i = markers_.begin(); i != end; ++i) {
    const Node* node = i->key;

    // inner loop: process each marker in the current node
    MarkerLists* markers = i->value.Get();
    for (DocumentMarker::MarkerType type : DocumentMarker::AllMarkers()) {
      DocumentMarkerList* const list = ListForType(markers, type);
      if (!list || list->IsEmpty() || !marker_types.Contains(type))
        continue;

      // cause the node to be redrawn
      if (LayoutObject* layout_object = node->GetLayoutObject()) {
        layout_object->SetShouldDoFullPaintInvalidation(
            PaintInvalidationReason::kDocumentMarker);
        break;
      }
    }
  }
}

bool DocumentMarkerController::SetTextMatchMarkersActive(
    const EphemeralRange& range,
    bool active) {
  if (!PossiblyHasMarkers(DocumentMarker::kTextMatch))
    return false;

  DCHECK(!markers_.IsEmpty());

  Node* const start_container = range.StartPosition().ComputeContainerNode();
  DCHECK(start_container);
  Node* const end_container = range.EndPosition().ComputeContainerNode();
  DCHECK(end_container);

  const unsigned container_start_offset =
      range.StartPosition().ComputeOffsetInContainerNode();
  const unsigned container_end_offset =
      range.EndPosition().ComputeOffsetInContainerNode();

  bool marker_found = false;
  for (Node& node : range.Nodes()) {
    int start_offset = node == start_container ? container_start_offset : 0;
    int end_offset = node == end_container ? container_end_offset : INT_MAX;
    marker_found |=
        SetTextMatchMarkersActive(&node, start_offset, end_offset, active);
  }
  return marker_found;
}

bool DocumentMarkerController::SetTextMatchMarkersActive(Node* node,
                                                         unsigned start_offset,
                                                         unsigned end_offset,
                                                         bool active) {
  MarkerLists* markers = markers_.at(node);
  if (!markers)
    return false;

  bool doc_dirty = false;
  DocumentMarkerList* const list =
      ListForType(markers, DocumentMarker::kTextMatch);

  if (!list)
    return false;

  const HeapVector<Member<DocumentMarker>>& markers_in_list =
      list->GetMarkers();
  // TODO(rlanday): this assumes that the markers are stored in sorted order.
  // This method should probably eventually be implemented by a
  // TextMatch-specific marker list
  const auto start_pos = std::upper_bound(
      markers_in_list.begin(), markers_in_list.end(), start_offset,
      [](size_t start_offset, const Member<DocumentMarker>& marker) {
        return start_offset < marker->EndOffset();
      });
  for (auto marker = start_pos; marker != markers_in_list.end(); ++marker) {
    // Markers are returned in order, so stop if we are now past the specified
    // range.
    if ((*marker)->StartOffset() >= end_offset)
      break;

    (*marker)->SetIsActiveMatch(active);
    doc_dirty = true;
  }

  // repaint the affected node
  if (doc_dirty && node->GetLayoutObject()) {
    node->GetLayoutObject()->SetShouldDoFullPaintInvalidation(
        PaintInvalidationReason::kDocumentMarker);
  }
  return doc_dirty;
}

#ifndef NDEBUG
void DocumentMarkerController::ShowMarkers() const {
  StringBuilder builder;
  MarkerMap::const_iterator end = markers_.end();
  for (MarkerMap::const_iterator node_iterator = markers_.begin();
       node_iterator != end; ++node_iterator) {
    const Node* node = node_iterator->key;
    builder.Append(String::Format("%p", node));
    MarkerLists* markers = markers_.at(node);
    for (DocumentMarker::MarkerType type : DocumentMarker::AllMarkers()) {
      DocumentMarkerList* const list = ListForType(markers, type);
      const HeapVector<Member<DocumentMarker>>& markers_in_list =
          list->GetMarkers();
      for (const DocumentMarker* marker : markers_in_list) {
        builder.Append(" ");
        builder.AppendNumber(marker->GetType());
        builder.Append(":[");
        builder.AppendNumber(marker->StartOffset());
        builder.Append(":");
        builder.AppendNumber(marker->EndOffset());
        builder.Append("](");
        builder.AppendNumber(marker->IsActiveMatch());
        builder.Append(")");
      }
    }
    builder.Append("\n");
  }
  LOG(INFO) << markers_.size() << " nodes have markers:\n"
            << builder.ToString().Utf8().data();
}
#endif

// SynchronousMutationObserver
void DocumentMarkerController::DidUpdateCharacterData(CharacterData* node,
                                                      unsigned offset,
                                                      unsigned old_length,
                                                      unsigned new_length) {
  if (!PossiblyHasMarkers(DocumentMarker::AllMarkers()))
    return;
  DCHECK(!markers_.IsEmpty());

  MarkerLists* markers = markers_.at(node);
  if (!markers)
    return;

  bool did_shift_marker = false;
  for (DocumentMarkerList* const list : *markers) {
    if (!list)
      continue;

    if (list->ShiftMarkers(offset, old_length, new_length))
      did_shift_marker = true;
  }

  if (!did_shift_marker)
    return;
  if (!node->GetLayoutObject())
    return;
  InvalidateRectsForTextMatchMarkersInNode(*node);
  // repaint the affected node
  node->GetLayoutObject()->SetShouldDoFullPaintInvalidation();
}

}  // namespace blink

#ifndef NDEBUG
void showDocumentMarkers(const blink::DocumentMarkerController* controller) {
  if (controller)
    controller->ShowMarkers();
}
#endif
